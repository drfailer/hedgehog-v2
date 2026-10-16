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

#ifndef HEDGEHOG_IMPL_GRAPH_TBB_RUNNER
#define HEDGEHOG_IMPL_GRAPH_TBB_RUNNER
#ifdef HH_ENABLE_TBB

#include <deque>
#include <atomic>
#include <algorithm>
#include <type_traits>
#include <tbb/task_arena.h>
#include <tbb/task_group.h>
#include "../../../graph/node.hpp"

namespace hh {

//
// Runner that runs the entire graph using a One-TBB thread pool. This runner
// implements a basic load balancer to control nodes execution and make sure
// single threaded nodes are only ran by a single thread (because they are
// considered thread safe).
//
struct TBBRunner {
    struct NodeData {
        Runnable         *runnable = nullptr;
        std::atomic<bool> busy{false};
        std::atomic<int>  active{0};
        std::atomic<int>  pending{0};
        std::atomic<bool> ready{false};
        int               max_concurrent = 0;
        size_t            max_threads    = 0;
        bool              parallel       = false;
    };

    std::deque<NodeData> node_data_   = {};
    std::vector<Node *>  graph_nodes_ = {};
    std::atomic<int>     total_demand_{0};
    tbb::task_arena      arena_;
    tbb::task_group      tg_;
    RuntimeInfo          runtime_info_ = {};

    TBBRunner(int num_threads = tbb::task_arena::automatic)
        : arena_(num_threads) {}

    void initialize(InitializationInfo const &) {}

    void register_node(auto node) {
        if constexpr (std::is_base_of_v<Runnable, std::remove_pointer_t<decltype(node)>>) {
            int idx = static_cast<int>(node_data_.size());
            node->runner_index(idx);

            bool parallel       = node->info().number_threads > 1;
            int  max_concurrent = static_cast<int>(node->info().number_threads);

            if (parallel) {
                node->resize_threads(static_cast<size_t>(arena_.max_concurrency()));
            }

            auto &nd = node_data_.emplace_back();
            nd.runnable       = node;
            nd.max_threads    = node->info().number_threads;
            nd.max_concurrent = max_concurrent;
            nd.parallel       = parallel;
        } else {
            graph_nodes_.push_back(node);
        }
    }

    void run(RunInfo const &info) {
        runtime_info_.run = info;

        for (auto *node : graph_nodes_) {
            node->run(info);
        }

        for (auto &nd : node_data_) {
            nd.busy.store(false, std::memory_order_relaxed);
            nd.active.store(0, std::memory_order_relaxed);
            nd.pending.store(0, std::memory_order_relaxed);
            nd.ready.store(false, std::memory_order_relaxed);
            for (size_t i = 0; i < nd.max_threads; ++i) {
                RuntimeInfo ri = runtime_info_;
                ri.thread_index = i;
                nd.runnable->initialize(ri);
            }
        }
    }

    //
    // Compute the amound of expected workers based on the total demand and the
    // node's thread configuration (a node with higher number_threads receives
    // more workers).
    //
    int expected_workers(size_t idx) {
        auto &nd = node_data_[idx];
        int  ttl = total_demand_.load(std::memory_order_relaxed);
        if (ttl <= 0) return nd.max_concurrent;
        return std::max(arena_.max_concurrency() * nd.max_concurrent / ttl, 1);
    }

    //
    // Mark the node `idx` ready and update the balancing parameters. Submit
    // the task when the node doesn't have enough active threads.
    //
    void make_node_unready(size_t idx) {
        auto &nd = node_data_[idx];
        if (nd.ready.exchange(false, std::memory_order_acq_rel)) {
            total_demand_.fetch_sub(nd.max_concurrent, std::memory_order_acq_rel);
        }
        if (nd.pending.load(std::memory_order_acquire) > 0) {
            if (!nd.ready.exchange(true, std::memory_order_acq_rel)) {
                total_demand_.fetch_add(nd.max_concurrent, std::memory_order_acq_rel);
            }
            submit_task(idx);
        }
    }

    //
    // Submit a node execution task to the TBB arena.
    //
    void submit_task(size_t idx) {
        arena_.execute([this, idx] {
            tg_.run([this, idx] {
                auto       &nd = node_data_[idx];
                RuntimeInfo ri = runtime_info_;

                if (nd.parallel) {
                    //
                    // Parallel node path: consume and process a batch of data
                    // and try to rebalance.
                    //

                    // Increase the active thread count on this node. If there
                    // are too many threads, we revert and leave.
                    int slot = nd.active.fetch_add(1, std::memory_order_acq_rel);
                    if (slot >= nd.max_concurrent) {
                        nd.active.fetch_sub(1, std::memory_order_acq_rel);
                        return;
                    }

                    ri.thread_index = static_cast<size_t>(
                        tbb::this_task_arena::current_thread_index());

                    // Loops while the current node has data and the workload
                    // is balance.
                    int processed = 0;
                    for (;;) {
                        int expected = expected_workers(idx);
                        int batch = std::max(
                            nd.pending.load(std::memory_order_relaxed) / std::max(expected, 1),
                            1);

                        // tries to execute a full batch and leave when the
                        // queue is empty (execute_one returns false).
                        for (int b = 0; b < batch; ++b) {
                            if (!nd.runnable->execute_one(ri)) goto done;
                            ++processed;
                        }

                        // when the amount of active threads on the node is
                        // greater than expected, we leave the node. Note that
                        // the amount of expected threads is dynamic (changes
                        // when new nodes become ready).
                        if (nd.active.load(std::memory_order_relaxed) > expected) break;
                    }
                    done:
                    // commit the number of elements processed
                    if (processed > 0) {
                        nd.pending.fetch_sub(processed, std::memory_order_acq_rel);
                    }

                    // leave the node
                    nd.active.fetch_sub(1, std::memory_order_acq_rel);

                    if (processed == 0) {
                        // make the node unready when 0 items where processed
                        // (the thread arrived on a empty node).
                        make_node_unready(idx);
                    } else if (nd.pending.load(std::memory_order_acquire) > 0) {
                        // if there is still data to process we re-submit the
                        // task to prevent data from getting stuck
                        submit_task(idx);
                    }
                } else {
                    //
                    // Serial node path: try to acquire ownership of the node
                    // and dries its input queue on success.
                    //

                    // get ownership
                    bool expected_val = false;
                    if (!nd.busy.compare_exchange_strong(
                            expected_val, true, std::memory_order_acq_rel))
                        return;

                    // consume data untill execute_all returns 0
                    ri.thread_index = 0;
                    for (;;) {
                        size_t count = nd.runnable->execute_all(ri);
                        if (count == 0) break;
                        nd.pending.fetch_sub(
                            static_cast<int>(count), std::memory_order_acq_rel);
                    }

                    // release ownership
                    nd.busy.store(false, std::memory_order_release);

                    // if new items have arrived (double check to avoid
                    // issues), we re-submit the task, otherwise we make it
                    // unready.
                    if (nd.pending.load(std::memory_order_acquire) > 0) {
                        submit_task(idx);
                    } else {
                        make_node_unready(idx);
                    }
                }
            });
        });
    }

    //
    // When on_transfer is called inside an edge, we know that we have new data
    // to process so we try to submit a node execution task inside the TBB
    // arena.
    //
    void on_transfer(Runnable *runnable, RuntimeInfo const &) {
        int idx = runnable->runner_index();
        if (idx < 0) return;
        auto &nd = node_data_[idx];
        int prev = nd.pending.fetch_add(1, std::memory_order_acq_rel);

        // update the node readyness and the total demand
        if (!nd.ready.exchange(true, std::memory_order_acq_rel)) {
            total_demand_.fetch_add(nd.max_concurrent, std::memory_order_acq_rel);
        }

        // we only submit the task if the nodes doesn't have enough active
        // threads to avoid flooding the task arena.
        int limit = expected_workers(static_cast<size_t>(idx));
        if (prev < limit) {
            submit_task(static_cast<size_t>(idx));
        }
    }

    void finalize(InitializationInfo const &) {
        arena_.execute([this] { tg_.wait(); });

        for (auto &nd : node_data_) {
            for (size_t i = 0; i < nd.max_threads; ++i) {
                RuntimeInfo ri = runtime_info_;
                ri.thread_index = i;
                nd.runnable->finalize(ri);
            }
        }

        total_demand_.store(0, std::memory_order_relaxed);
    }

    void on_result() {}
};

} // end namespace hh

#endif
#endif
