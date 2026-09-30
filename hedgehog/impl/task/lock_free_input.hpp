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

#ifndef HEDGEHOG_IMPL_TASK_LOCK_FREE_INPUT_H
#define HEDGEHOG_IMPL_TASK_LOCK_FREE_INPUT_H

#include "trigger.hpp"
#include "queue.hpp"
#include "lock_queue_input.hpp"
#include "../../tool/macros.hpp"

#include "../../../3rdparty/daking_mpsc_queue/MPSC_queue.hpp"
#include "../../../3rdparty/moodycamel_concurrent_queue/concurrentqueue.h"

namespace hh {

// bounded lock free queue port ////////////////////////////////////////////////

template <size_t Size, typename T>
struct BoundedLockFreeQueueInputPort {
    BoundedLockFreeQueue<data_t<T>, Size> queue;
    size_t max_queue_size = 0;

    void push_data(data_t<T> data, RuntimeInfo const &) {
        queue.push(std::move(data));
        #ifdef HH_ENABLE_PROFILING
        size_t size = queue.size();
        if (size > max_queue_size) max_queue_size = size;
        #endif
    }

    // TODO: change the pop signature
    std::optional<data_t<T>> pop() {
        return queue.pop();
    }

    size_t size() {
        return queue.size();
    }
};

template <size_t Size, typename ...Inputs>
struct BoundedLockFreeQueueInputPorts {
    template <typename T> using Queue = BoundedLockFreeQueueInputPort<Size, T>;
    std::tuple<std::unique_ptr<Queue<Inputs>>...> ports_;

    BoundedLockFreeQueueInputPorts()
        : ports_(std::make_unique<Queue<Inputs>>()...) {}

    template <typename T>
    Queue<T> *port() {
        return std::get<std::unique_ptr<Queue<T>>>(ports_).get();
    }

    void initialize(InitializationInfo const &) {
        ([] (auto p) { p->max_queue_size = 0; }(port<Inputs>()), ...);
    }

    void finalize([[maybe_unused]] InitializationInfo const &info) {
        #ifdef HH_ENABLE_PROFILING
        ([&] {
            using namespace std::string_literals;
            auto *p = port<Inputs>();
            info.profiler->add_string(
                "BoundedLockFreeQueueInputPort<"s + type_to_string<Inputs>() + ">",
                "MQS = ", p->max_queue_size, " | ", "QS = ", p->size()
            );
        }(), ...);
        #endif
    }

    template <typename Executable>
    void execute(Executable exec, [[maybe_unused]] RuntimeInfo const &info) {
        ([&] {
            if (auto data = port<Inputs>()->pop()) [[likely]] {
                exec->execute(std::move(*data));
            }
        }(), ...);
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        port<T>()->push_data(std::move(data), info);
    }
};

// bounded lock free queue input (sema) ////////////////////////////////////////

template <size_t Size, typename ...Inputs>
struct BoundedLockFreeQueueInput : SemaTrigger, BoundedLockFreeQueueInputPorts<Size, Inputs...> {
    void initialize(InitializationInfo const &info) {
        BoundedLockFreeQueueInputPorts<Size, Inputs...>::initialize(info);
        SemaTrigger::initialize(info.node->number_threads);
    }

    void finalize([[maybe_unused]] InitializationInfo const &info) {
        SemaTrigger::finalize();
        BoundedLockFreeQueueInputPorts<Size, Inputs...>::finalize(info);
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        this->template port<T>()->push_data(std::move(data), info);
        SemaTrigger::signal(SignalOpts{1, 0});
    }
};

// bounded lock free queue input (futex) ///////////////////////////////////////

template <size_t Size, size_t SpinCount, typename ...Inputs>
struct BoundedLockFreeFutexInput : FutexTrigger<SpinCount>, BoundedLockFreeQueueInputPorts<Size, Inputs...> {
    using Trigger = FutexTrigger<SpinCount>;

    void initialize(InitializationInfo const &info) {
        BoundedLockFreeQueueInputPorts<Size, Inputs...>::initialize(info);
        Trigger::initialize(info.node->number_threads);
    }

    void finalize([[maybe_unused]] InitializationInfo const &info) {
        Trigger::finalize();
        BoundedLockFreeQueueInputPorts<Size, Inputs...>::finalize(info);
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        this->template port<T>()->push_data(std::move(data), info);
        Trigger::signal(SignalOpts{1, 0});
    }
};

// moodycamel mpmc input ///////////////////////////////////////////////////////

template <typename T>
struct MoodycamelMPMCInputPort {
    moodycamel::ConcurrentQueue<data_t<T>> queue{};
    size_t max_queue_size = 0;

    void initialize(InitializationInfo const &) {
        // TODO: with info, we can get the number of threads on the task which we can use to create consumer tokens
        max_queue_size = 0;
    }

    void finalize(InitializationInfo const &info) {
        #ifdef HH_ENABLE_PROFILING
        using namespace std::string_literals;
        info.profiler->add_string(
            type_to_string<decltype(*this)>(),
            "MQS = ", max_queue_size, " | ", "QS = ", queue.size_approx()
        );
        #endif
    }

    void push_data(data_t<T> data, RuntimeInfo const &) {
        // TODO: get consumer token with the thread index
        queue.enqueue(std::move(data));
        #ifdef HH_ENABLE_PROFILING
        size_t size = queue.size_approx();
        if (size > max_queue_size) max_queue_size = size;
        #endif
    }

    // TODO: bulk push

    bool pop(data_t<T> &data) {
        return queue.try_dequeue(data);
    }

    size_t size() {
        return queue.size_approx();
    }
};

template <typename ...Inputs>
struct MoodycamelMPMCInput : CondTrigger, MoodycamelMPMCInputPort<Inputs>... {
    void initialize(InitializationInfo const &info) {
        (MoodycamelMPMCInputPort<Inputs>::initialize(info), ...);
        CondTrigger::initialize();
    }

    void finalize([[maybe_unused]] InitializationInfo const &info) {
        CondTrigger::finalize();
        (MoodycamelMPMCInputPort<Inputs>::finalize(info), ...);
    }

    WaitResult wait([[maybe_unused]] RuntimeInfo const &info) {
        return CondTrigger::wait([this]{
            return ((MoodycamelMPMCInputPort<Inputs>::size() > 0) || ...);
        });
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        MoodycamelMPMCInputPort<T>::push_data(std::move(data), info);
        CondTrigger::signal(SignalOpts{1, 0});
    }

    template <typename Executable>
    void execute(Executable exec, [[maybe_unused]] RuntimeInfo const &info) {
        ([&] {
            data_t<Inputs> data;
            while (MoodycamelMPMCInputPort<Inputs>::pop(data)) [[likely]] {
                exec->execute(std::move(data));
            }
        }(), ...);
    }
};

// daking mpsc input ///////////////////////////////////////////////////////////

template <typename T>
struct DakingMPSCInputPort {
    daking::MPSC_queue<data_t<T>> queue;

    void push_data(data_t<T> data, RuntimeInfo const &) {
        queue.enqueue(std::move(data));
    }

    // TODO: bulk push

    bool pop(data_t<T> &data) {
        return queue.try_dequeue(data);
    }

    bool empty() {
        return queue.empty();
    }
};

template <typename ...Inputs>
struct DakingMPSCInput : CondTrigger, DakingMPSCInputPort<Inputs>... {
    void initialize(InitializationInfo const &info) {
        CondTrigger::initialize();
    }

    void finalize([[maybe_unused]] InitializationInfo const &info) {
        CondTrigger::finalize();
    }

    WaitResult wait([[maybe_unused]] RuntimeInfo const &info) {
        return CondTrigger::wait([this]{
            return ((!DakingMPSCInputPort<Inputs>::empty()) || ...);
        });
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        DakingMPSCInputPort<T>::push_data(std::move(data), info);
        CondTrigger::signal(SignalOpts{1, 0});
    }

    template <typename Executable>
    void execute(Executable exec, [[maybe_unused]] RuntimeInfo const &info) {
        ([&] {
            data_t<Inputs> data;
            while (DakingMPSCInputPort<Inputs>::pop(data)) [[likely]] {
                exec->execute(std::move(data));
            }
        }(), ...);
    }
};

} // end namespace hh

#endif
