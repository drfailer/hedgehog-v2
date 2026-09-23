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
#include <semaphore>
#include "../../graph/node.hpp"
#include "../../tool/log.hpp"
#include "trigger.hpp"

namespace hh {

template <typename T>
struct Edge;

// lock queue input port ///////////////////////////////////////////////////////

template <typename T>
struct alignas(64) LockQueueInputPort {
    std::mutex mutex;
    std::queue<data_t<T>> queue;
    size_t max_queue_size = 0;

    void push_data(data_t<T> data, RuntimeInfo const &) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(std::move(data));
#ifdef HH_ENABLE_PROFILING
        if (queue.size() > max_queue_size) max_queue_size = queue.size();
#endif
    }

    std::optional<data_t<T>> pop() {
        std::lock_guard<std::mutex> lock(mutex);
        if (queue.empty()) [[unlikely]] return std::nullopt;
        auto data = std::move(queue.front());
        queue.pop();
        return data;
    }

    size_t size() {
        return queue.size();
    }
};


template <typename ...Inputs>
struct LockQueueInputPorts : NodePorts<LockQueueInputPort, Inputs...> {
    void initialize(InitializationInfo const &) {
        ([&] { LockQueueInputPort<Inputs>::max_queue_size = 0; }(), ...);
    }

    void finalize([[maybe_unused]] InitializationInfo const &info) {
        #ifdef HH_ENABLE_PROFILING
        ([&] {
            using namespace std::string_literals; // for ""s
            auto profile = info.profiler->profile("LockQueueInputPort<"s + type_to_string<Inputs>() + ">");
            profile->set_info("MQS = ", LockQueueInputPort<Inputs>::max_queue_size, " | ", "QS = ", LockQueueInputPort<Inputs>::size());
        }(), ...);
        #endif
    }

    template <typename Executable>
    void execute(Executable exec, [[maybe_unused]] RuntimeInfo const &info) {
        ([&] {
            if (auto data = LockQueueInputPort<Inputs>::pop()) [[likely]] {
                exec->execute(std::move(*data));
            }
        }(), ...);
    }
};

// cond trigger input //////////////////////////////////////////////////////////

template <typename ...Inputs>
struct LockQueueNodeInput : CondTrigger, LockQueueInputPorts<Inputs...> {
    void initialize(InitializationInfo const &info) {
        LockQueueInputPorts<Inputs...>::initialize(info);
        CondTrigger::initialize([this]{
            return ((LockQueueInputPort<Inputs>::size() > 0) || ...);
        });
    }

    void finalize([[maybe_unused]] InitializationInfo const &info) {
        CondTrigger::finalize();
        LockQueueInputPorts<Inputs...>::finalize(info);
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        LockQueueInputPort<T>::push_data(std::move(data), info);
        CondTrigger::signal(SignalOpts{1, 0});
    }
};


// sema trigger input //////////////////////////////////////////////////////////

template <typename ...Inputs>
struct LockQueueSemaNodeInput : SemaTrigger, LockQueueInputPorts<Inputs...> {
    void initialize(InitializationInfo const &info) {
        LockQueueInputPorts<Inputs...>::initialize(info);
        SemaTrigger::initialize(info.node->number_threads);
    }

    void finalize([[maybe_unused]] InitializationInfo const &info) {
        SemaTrigger::finalize();
        LockQueueInputPorts<Inputs...>::finalize(info);
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        LockQueueInputPort<T>::push_data(std::move(data), info);
        SemaTrigger::signal(SignalOpts{1, 0});
    }
};

} // end namespace

#endif
