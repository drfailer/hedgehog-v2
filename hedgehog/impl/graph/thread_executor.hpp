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

#ifndef HEDGEHOG_IMPL_GRAPH_THREAD_EXECUTOR
#define HEDGEHOG_IMPL_GRAPH_THREAD_EXECUTOR

#include <vector>
#include <thread>
#include "../../tool/numa.hpp"

namespace hh {

struct ThreadExecutor {
    std::vector<std::thread> threads = {};
    std::vector<TaskHandle> handles_ = {};
    std::vector<Node *> graph_nodes_ = {};

    void initialize(InitializationInfo const &) {}

    void register_node(auto node) {
        if constexpr (requires { node->handle(); }) {
            handles_.push_back(node->handle());
        } else {
            graph_nodes_.push_back(node);
        }
    }

    void execute(ExecutionInfo const &info) {
        for (auto *node : graph_nodes_) {
            node->execute(info);
        }

        for (auto &handle : handles_) {
            for (size_t i = 0; i < handle.task->info().number_threads; ++i) {
                auto pipeline = info.pipeline;
                threads.push_back(
                    std::thread([&handle, i, info, pipeline]() {
                        numa::pin_current_thread(pipeline.numa_id);
                        RuntimeInfo ri{};
                        ri.exec = info;
                        ri.thread_index = i;
                        handle.initialize(handle.task, ri);
                        for (;;) {
                            auto wr = handle.wait(handle.task, ri);
                            if (wr.terminate) [[unlikely]] break;
                            if (!wr.skip) [[unlikely]] handle.execute(handle.task, ri);
                        }
                        handle.finalize(handle.task, ri);
                    })
                );
            }
        }
    }

    void finalize(InitializationInfo const &) {
        for (auto &thread : threads) {
            thread.join();
        }
        threads.clear();
        handles_.clear();
        graph_nodes_.clear();
    }
};

} // end namespace hh

#endif
