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

#ifndef HEDGEHOG_IMPL_GRAPH_THREAD_RUNNER
#define HEDGEHOG_IMPL_GRAPH_THREAD_RUNNER

#include <vector>
#include <thread>
#include <type_traits>
#include "../../tool/numa.hpp"

namespace hh {

struct ThreadRunner {
    struct RunEntry {
        Runnable *runnable;
        size_t number_threads;
    };

    std::vector<std::thread> threads = {};
    std::vector<RunEntry> runnables_ = {};
    std::vector<Node *> graph_nodes_ = {};

    void initialize(InitializationInfo const &) {}

    void register_node(auto node) {
        if constexpr (std::is_base_of_v<Runnable, std::remove_pointer_t<decltype(node)>>) {
            runnables_.push_back({node, node->info().number_threads});
        } else {
            graph_nodes_.push_back(node);
        }
    }

    void run(RunInfo const &info) {
        for (auto *node : graph_nodes_) {
            node->run(info);
        }

        for (auto &entry : runnables_) {
            for (size_t i = 0; i < entry.number_threads; ++i) {
                auto pipeline = info.pipeline;
                auto *runnable = entry.runnable;
                threads.push_back(
                    std::thread([runnable, i, info, pipeline]() {
                        numa::pin_current_thread(pipeline.numa_id);
                        RuntimeInfo ri{};
                        ri.run = info;
                        ri.thread_index = i;
                        runnable->initialize(ri);
                        for (;;) {
                            auto wr = runnable->wait(ri);
                            if (wr.terminate) [[unlikely]] break;
                            if (wr.skip) [[unlikely]] continue;
                            runnable->execute_all(ri);
                        }
                        runnable->finalize(ri);
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
        runnables_.clear();
        graph_nodes_.clear();
    }
};

} // end namespace hh

#endif
