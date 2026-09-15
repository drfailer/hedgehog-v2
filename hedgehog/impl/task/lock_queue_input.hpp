// NIST-developed software is provided by NIST as a public service. You may use, copy and distribute copies of the
// software in any medium, provided that you keep intact this entire notice. You may improve, modify and create
// derivative works of the software or any portion of the software, and you may copy and distribute such modifications
// or works. Modified works should carry a notice stating that you changed the software and should note the date and
// nature of any such change. Please explicitly acknowledge the National Institute of Standards and Technology as the
// source of the software. NIST-developed software is expressly provided "AS IS." NIST MAKES NO WARRANTY OF ANY KIND,
// EXPRESS, IMPLIED, IN FACT OR ARISING BY OPERATION OF LAW, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTY OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, NON-INFRINGEMENT AND DATA ACCURACY. NIST NEITHER REPRESENTS NOR
// WARRANTS THAT THE OPERATION OF THE SOFTWARE WILL BE UNINTERRUPTED OR ERROR-FREE, OR THAT ANY DEFECTS WILL BE
// CORRECTED. NIST DOES NOT WARRANT OR MAKE ANY REPRESENTATIONS REGARDING THE USE OF THE SOFTWARE OR THE RESULTS
// THEREOF, INCLUDING BUT NOT LIMITED TO THE CORRECTNESS, ACCURACY, RELIABILITY, OR USEFULNESS OF THE SOFTWARE. You
// are solely responsible for determining the appropriateness of using and distributing the software and you assume
// all risks associated with its use, including but not limited to the risks and costs of program errors, compliance
// with applicable laws, damage to or loss of data, programs or equipment, and the unavailability or interruption of
// operation. This software is not intended to be used in any situation where a failure could cause risk of injury or
// damage to property. The software developed by NIST employees is not subject to copyright protection within the
// United States.

#ifndef HEDGEHOG_IMPL_TASK_LOCK_QUEUE_INPUT
#define HEDGEHOG_IMPL_TASK_LOCK_QUEUE_INPUT

#include <vector>
#include <mutex>
#include <queue>
#include <condition_variable>
#include "../../graph/node.hpp"
#include "../../tool/log.hpp"

namespace hh {

template <typename T>
struct Edge;

template <typename T>
struct LockQueueInputPort {
    std::mutex mutex;
    std::queue<data_t<T>> queue;
    size_t max_queue_size = 0;

    void push_data(data_t<T> data, RuntimeInfo const &info) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(std::move(data));
        if (queue.size() > max_queue_size) max_queue_size = queue.size();
    }

    std::optional<data_t<T>> pop() {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) return std::nullopt;
        auto data = std::move(queue.front());
        queue.pop();
        return data;
    }

    size_t size() {
        std::lock_guard<std::mutex> lock(mutex); // this is comment out in hh??
        return queue.size();
    }
};

struct CondTrigger {
    std::mutex mutex{};
    std::condition_variable cond{};
    bool terminated = false;
    std::function<bool()> pred;

    void initialize(std::function<bool()> fun) {
        this->pred = fun;
        terminated = false;
    }

    void finalize() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            terminated = true;
        }
        cond.notify_all();
    }

    void signal(SignalOpts const &opts) {
        std::lock_guard<std::mutex> lock(mutex); // lock to avoid lost wakeup
        if (opts.count == 1) {
            cond.notify_one();
        } else {
            cond.notify_all();
        }
    }

    WaitResult wait([[maybe_unused]] RuntimeInfo const &info) {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this]{
            return pred() || terminated;
        });
        return WaitResult{terminated, false};
    }
};

template <typename ...Inputs>
struct LockQueueNodeInput : CondTrigger, NodePorts<LockQueueInputPort, Inputs...> {
    void initialize(InitializationInfo const &info) {
        ([&] { LockQueueInputPort<Inputs>::max_queue_size = 0; }(), ...);
        CondTrigger::initialize([this]{
            return ((LockQueueInputPort<Inputs>::size() > 0) || ...);
        });
    }

    void finalize(InitializationInfo const &info) {
        CondTrigger::finalize();
#ifdef HH_ENABLE_PROFILING
        ([&] {
            using namespace std::string_literals; // for ""s
            auto profile = info.profiler->profile("LockQueueInputPort<"s + type_to_string<Inputs>() + ">");
            profile->set_info("MQS = ", LockQueueInputPort<Inputs>::max_queue_size, " | ", "QS = ", LockQueueInputPort<Inputs>::size());
        }(), ...);
#endif
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        LockQueueInputPort<T>::push_data(std::move(data), info);
        CondTrigger::signal(SignalOpts{info, 1, 0});
    }

    template <typename Executable>
    void execute(Executable exec, [[maybe_unused]] RuntimeInfo const &info) {
        ([&] {
            if (auto data = LockQueueInputPort<Inputs>::pop()) {
                exec->execute(std::move(*data));
            }
        }(), ...);
    }
};

} // end namespace

#endif
