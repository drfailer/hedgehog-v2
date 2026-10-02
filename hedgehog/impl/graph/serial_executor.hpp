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

#ifndef HEDGEHOG_IMPL_GRAPH_SERIAL_EXECUTOR_H
#define HEDGEHOG_IMPL_GRAPH_SERIAL_EXECUTOR_H

#include <set>
#include <queue>
#include <thread>
#include <cstdio>
#include <unordered_map>
#include "../../graph/node.hpp"

namespace hh {

struct SerialExecutor {
    std::vector<TaskHandle> handles_ = {};
    std::vector<Node *> graph_nodes_ = {};
    std::unordered_map<Node *, size_t> handle_map_ = {};
    std::queue<size_t> ready_handles_;
    bool executing_ = false;
    RuntimeInfo runtime_info_ = {};

    void initialize(InitializationInfo const &) {
        executing_ = false;
    }

    template <typename ConcreteNode>
    void register_node(ConcreteNode *node) {
        if constexpr (requires { node->handle(); }) {
            handle_map_[node] = handles_.size();
            handles_.push_back(node->handle());
        } else {
            graph_nodes_.push_back(node);
        }
    }

    void execute(ExecutionInfo const &info) {
        runtime_info_.exec = info;
        runtime_info_.thread_index = 0;

        for (auto *node : graph_nodes_) {
            node->execute(info);
        }
        for (auto &handle : handles_) {
            handle.initialize(handle.task, runtime_info_);
        }
    }

    void on_transfer(Node *node, RuntimeInfo const &) {
        auto it = handle_map_.find(node);
        if (it == handle_map_.end()) return;
        ready_handles_.push(it->second);
        execute_ready_handles();
    }

    void execute_ready_handles() {
        if (executing_) return;
        executing_ = true;
        while (!ready_handles_.empty()) {
            auto idx = ready_handles_.front();
            ready_handles_.pop();
            auto &handle = handles_[idx];
            handle.execute(handle.task, runtime_info_);
        }
        executing_ = false;
    }

    void on_result() {
        execute_ready_handles();
    }

    void finalize(InitializationInfo const &) {
        for (auto &handle : handles_) {
            handle.finalize(handle.task, runtime_info_);
        }
        handles_.clear();
        graph_nodes_.clear();
        handle_map_.clear();
    }
};

} // end namespace hh

#endif
