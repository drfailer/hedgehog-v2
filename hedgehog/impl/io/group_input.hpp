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

#ifndef HEDGEHOG_IMPL_TASK_GROUP_INPUT_H
#define HEDGEHOG_IMPL_TASK_GROUP_INPUT_H

#include <vector>
#include <unistd.h>

namespace hh {

//
// This input creates small groups of threads and attribute input ports (queues
// per type) to each group. This allows to reduce contention when there are a
// lot of threads.
//

template <size_t GroupSize, typename InputType>
struct GroupNodeInput {
    inline thread_local static size_t index_ = 0;
    size_t num_groups_ = 0;
    std::vector<std::unique_ptr<InputType>> groups_ = {};

    void initialize(InitializationInfo const &info) {
        size_t number_threads = info.node->number_threads;
        num_groups_ = (number_threads + GroupSize - 1) / GroupSize;
        groups_.resize(num_groups_);

        auto node_cpy = *info.node;
        auto info_cpy = info;
        info_cpy.node = &node_cpy;

        for (size_t g = 0; g < num_groups_; ++g) {
            node_cpy.number_threads = std::min(GroupSize, number_threads - g * GroupSize);
            groups_[g] = std::make_unique<InputType>();
            groups_[g]->initialize(info);
        }
    }

    void finalize(InitializationInfo const &info) {
        for (auto &group : groups_) {
            group->finalize(info);
        }
    }

    WaitResult wait(RuntimeInfo const &info) {
        return groups_[info.thread_index / GroupSize]->wait(info);
    }

    void signal(SignalOpts const &opts) {
        size_t count = opts.count;
        while (count > 0) {
            size_t group_index = index_++ % num_groups_;
            auto &group = groups_[group_index];
            size_t signal_count = std::min(count, group->number_threads);
            group->signal({signal_count, 0});
            count -= signal_count;
        }
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        size_t group_index = index_++ % num_groups_;
        groups_[group_index]->template push_data<T>(std::move(data), info);
    }

    template <typename Executable>
    size_t execute(Executable exec, [[maybe_unused]] RuntimeInfo const &info) {
        return groups_[info.thread_index / GroupSize]->execute(exec, info);
    }
};

} // end namespace hh

#endif
