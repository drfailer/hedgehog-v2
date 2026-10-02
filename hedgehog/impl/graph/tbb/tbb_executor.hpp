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

#ifndef HEDGEHOG_IMPL_GRAPH_TBB_EXECUTOR
#define HEDGEHOG_IMPL_GRAPH_TBB_EXECUTOR

#include <deque>
#include <atomic>
#include <tbb/task_arena.h>
#include <tbb/task_group.h>
#include <tbb/concurrent_queue.h>
#include "../../../graph/node.hpp"

namespace hh {

struct TBBExecutor {
    struct NodeData {
        TaskHandle handle = {};
        tbb::concurrent_queue<int> free_slots = {};
        std::atomic<int> pending{0};
        size_t max_threads = 0;

        int acquire_slot() {
            int slot;
            return free_slots.try_pop(slot) ? slot : -1;
        }

        void release_slot(int slot) {
            free_slots.push(slot);
        }
    };

    std::deque<NodeData> node_data_ = {};
    std::vector<Node *> graph_nodes_ = {};
    tbb::task_arena arena_;
    tbb::task_group tg_;
    RuntimeInfo runtime_info_ = {};

    TBBExecutor(int num_threads = tbb::task_arena::automatic)
        : arena_(num_threads) {}

    void initialize(InitializationInfo const &) {}

    void register_node(auto node) {
        if constexpr (requires { node->handle(); }) {
            int idx = static_cast<int>(node_data_.size());
            node->executor_index(idx);
            auto &nd = node_data_.emplace_back();
            nd.handle = node->handle();
            nd.max_threads = node->info().number_threads;
            for (size_t i = 0; i < nd.max_threads; ++i) { nd.free_slots.push(static_cast<int>(i)); }
        } else {
            graph_nodes_.push_back(node);
        }
    }

    void execute(ExecutionInfo const &info) {
        runtime_info_.exec = info;

        for (auto *node : graph_nodes_) {
            node->execute(info);
        }

        for (auto &nd : node_data_) {
            for (size_t i = 0; i < nd.max_threads; ++i) {
                RuntimeInfo ri = runtime_info_;
                ri.thread_index = i;
                nd.handle.initialize(nd.handle.task, ri);
            }
        }
    }

    void submit_task(size_t idx) {
        arena_.execute([this, idx] {
            tg_.run([this, idx] {
                auto &nd = node_data_[idx];
                int slot = nd.acquire_slot();
                if (slot < 0) return;

                RuntimeInfo ri = runtime_info_;
                ri.thread_index = static_cast<size_t>(slot);

                for (;;) {
                    size_t count = nd.handle.execute(nd.handle.task, ri);
                    if (count == 0) break;
                    nd.pending.fetch_sub(static_cast<int>(count), std::memory_order_acq_rel);
                }

                nd.release_slot(slot);

                if (nd.pending.load(std::memory_order_acquire) > 0) {
                    submit_task(idx);
                }
            });
        });
    }

    void on_transfer(Node *node, RuntimeInfo const &) {
        int idx = node->executor_index();
        if (idx < 0) return;
        node_data_[idx].pending.fetch_add(1, std::memory_order_release);
        submit_task(static_cast<size_t>(idx));
    }

    void finalize(InitializationInfo const &) {
        arena_.execute([this] { tg_.wait(); });

        for (auto &nd : node_data_) {
            for (size_t i = 0; i < nd.max_threads; ++i) {
                RuntimeInfo ri = runtime_info_;
                ri.thread_index = i;
                nd.handle.finalize(nd.handle.task, ri);
            }
        }

        node_data_.clear();
        graph_nodes_.clear();
    }
};

} // end namespace hh

#endif
