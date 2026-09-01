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

namespace hh {

template <typename T>
struct LockQueueInputPort {
    std::mutex mutex;
    std::queue<std::shared_ptr<T>> queue;

    void push(std::shared_ptr<T> data) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(data);
    }

    std::optional<std::shared_ptr<T>> pop() {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) return std::nullopt;
        auto data = queue.front();
        queue.pop();
        return data;
    }

    size_t size() {
        std::lock_guard<std::mutex> lock(mutex); // this is comment out in hh??
        return queue.size();
    }

    void connect_edge(Edge<T>) {}
};

template <typename ...Inputs>
struct LockQueueNodeInput : NodePorts<LockQueueInputPort, Inputs...> {
    std::mutex mutex{};
    std::condition_variable cond{};
    alignas(64) std::atomic<bool> terminated{false};

    void initialize(InitializationInfo const &) {
        terminated.store(false);
    }

    void finalize(InitializationInfo const &) {
        terminated.store(true);
    }

    void signal(SignalOpts const &opts) {
        std::lock_guard<std::mutex> lock(mutex); // lock to avoid false wakeup
        if (opts.count == 1) {
            cond.notify_one();
        } else {
            cond.notify_all();
        }
    }

    WaitResult wait(RuntimeInfo const &) {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this]{
            return has_data() || terminated.load(std::memory_order_acquire);
        });
        return WaitResult{terminated.load(std::memory_order_relaxed), false};
    }

    bool has_data() {
        return ((LockQueueInputPort<Inputs>::size() > 0) && ...);
    }

    template <typename T>
    void push_data(std::shared_ptr<T> data, RuntimeInfo const &) {
        LockQueueInputPort<T>::push(data);
    }

    template <typename Core>
    void execute_consumers(std::shared_ptr<Core> core, RuntimeInfo const &) {
        ([this, core] {
            if (auto data = LockQueueInputPort<Inputs>::pop()) {
                core->execute(*data);
            }
        }, ...);
    }
};

} // end namespace

#endif
