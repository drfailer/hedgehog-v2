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
#include <memory>
#include <tuple>
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
        // we don't lock here because the size is always acquired under the
        // trigger mutex
        return queue.size();
    }
};


template <typename ...Inputs>
struct LockQueueInputPorts : NodePorts<LockQueueInputPort, Inputs...> {
    void initialize(InitializationInfo const &) {
        ([this] () { LockQueueInputPort<Inputs>::max_queue_size = 0; }, ...);
    }

    void finalize([[maybe_unused]] InitializationInfo const &info) {
        #ifdef HH_ENABLE_PROFILING
        ([&] {
            using namespace std::string_literals;
            auto *p = static_cast<LockQueueInputPort<Inputs> *>(this);
            auto profile = info.profiler->profile("LockQueueInputPort<"s + type_to_string<Inputs>() + ">");
            profile->set_info("MQS = ", p->max_queue_size, " | ", "QS = ", p->size());
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

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        LockQueueInputPort<T>::push_data(std::move(data), info);
    }
};

// cond trigger input //////////////////////////////////////////////////////////

template <typename ...Inputs>
struct LockQueueNodeInput : CondTrigger, LockQueueInputPorts<Inputs...> {
    void initialize(InitializationInfo const &info) {
        LockQueueInputPorts<Inputs...>::initialize(info);
        CondTrigger::initialize();
    }

    void finalize([[maybe_unused]] InitializationInfo const &info) {
        CondTrigger::finalize();
        LockQueueInputPorts<Inputs...>::finalize(info);
    }

    WaitResult wait([[maybe_unused]] RuntimeInfo const &info) {
        return CondTrigger::wait([this]{
            return ((LockQueueInputPort<Inputs>::size() > 0) || ...);
        });
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

// futex trigger input /////////////////////////////////////////////////////////

template <typename SpinCount, typename ...Inputs>
struct LockQueueFutexNodeInput : FutexTrigger<SpinCount::value>, LockQueueInputPorts<Inputs...> {
    using Trigger = FutexTrigger<SpinCount::value>;

    void initialize(InitializationInfo const &info) {
        LockQueueInputPorts<Inputs...>::initialize(info);
        Trigger::initialize(info.node->number_threads);
    }

    void finalize([[maybe_unused]] InitializationInfo const &info) {
        Trigger::finalize();
        LockQueueInputPorts<Inputs...>::finalize(info);
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        LockQueueInputPort<T>::push_data(std::move(data), info);
        Trigger::signal(SignalOpts{1, 0});
    }
};

// group input /////////////////////////////////////////////////////////////////

//
// This input creates small groups of threads and attribute input ports (queues
// per type) to each group. This allows to reduce contention when there are a
// lot of threads.
//

template <size_t GroupSize, typename TriggerType, template <typename ...> class PortsType, typename ...Inputs>
struct GroupNodeInput {
    struct Group {
        PortsType<Inputs...> ports;
        TriggerType trigger;
        size_t number_threads;

        void initialize(InitializationInfo const &info, size_t number_threads) {
            this->number_threads = number_threads;
            if constexpr (requires { trigger.initialize(number_threads); }) {
                trigger.initialize(number_threads);
            } else {
                trigger.initialize();
            }
            ports.initialize(info);
        }

        void finalize(InitializationInfo const &info) {
            trigger.finalize();
            ports.finalize(info);
        }
    };

    size_t number_threads_ = 0;
    std::vector<std::unique_ptr<Group>> groups_ = {};
    alignas(64) std::atomic<size_t> index_{0};

    void initialize(InitializationInfo const &info) {
        size_t number_threads = info.node->number_threads;
        this->number_threads_ = number_threads;
        groups_.resize((number_threads / GroupSize) + ((number_threads % GroupSize) == 0 ? 0 : 1));
        for (auto &group : groups_) {
            group = std::make_unique<Group>();
            group->initialize(info, std::min(number_threads, GroupSize));
            number_threads -= GroupSize;
        }
    }

    void finalize(InitializationInfo const &info) {
        for (auto &group : groups_) {
            group->finalize(info);
        }
    }

    WaitResult wait(RuntimeInfo const &info) {
        return groups_[info.exec.thread_index / GroupSize]->trigger.wait(info);
    }

    void signal(SignalOpts const &opts) {
        size_t count = opts.count;

        while (count > 0) {
            // the index is slightly off if the number of threads is not
            // divisible by the group count, however, it shouldn't cause any
            // issues.
            size_t group_index = (index_.fetch_add(std::min(count, GroupSize)) % number_threads_) / GroupSize;
            auto &group = groups_[group_index];
            size_t signal_count = std::min(count, group.number_threads);
            group->trigger.signal({0, signal_count});
            count -= signal_count;
        }
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        size_t group_index = (index_.fetch_add(1) % number_threads) / GroupSize;
        groups_[group_index]->ports.template push_data<T>(std::move(data), info);
        groups_[group_index]->trigger.signal({1, 0});
    }

    template <typename Executable>
    void execute(Executable exec, [[maybe_unused]] RuntimeInfo const &info) {
        groups_[info.exec.thread_index / GroupSize]->ports.execute(exec, info);
    }
};


template <typename ...Inputs>
using LockGroupNodeInput = GroupNodeInput<4, SemaTrigger, LockQueueInputPorts, Inputs...>;

} // end namespace

#endif
