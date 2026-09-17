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
#include "../../graph/node.hpp"

namespace hh {

struct SerialExecutor {
    std::set<std::shared_ptr<Node>> const *nodes_ = nullptr;
    std::queue<Node *> ready_nodes_;
    bool executing_;

    ExecutionInfo make_execution_info(auto phase) {
        return ExecutionInfo{0, 0, {0, 0}, true, phase};
    }

    void execute(std::set<std::shared_ptr<Node>> const &nodes, ExecutionInfo const &) {
        // graphs are executed only once (a graph node never ends up in the
        // ready list) so here we simply initialize the tasks and sub-graphs
        nodes_ = &nodes;
        for (auto &node : nodes) {
            node->execute(make_execution_info(ExecutionInfo::Initialize));
        }
    }

    void on_transfer(Node *node, RuntimeInfo const &) {
        // we don't execute the node directly here, otherwize cyclic graphs
        // with a lot of data would stack overflow.
        ready_nodes_.push(node);
        execute_ready_nodes(); // we can try to start the execution
    }

    void execute_ready_nodes() {
        if (executing_) {
            // here, we already are in the execution loop and we must leave to
            // prevent recursion
            return;
        }
        executing_ = true;
        while (!ready_nodes_.empty()) {
            auto node = ready_nodes_.front();
            ready_nodes_.pop();
            node->execute(make_execution_info(ExecutionInfo::Execute));
        }
        executing_ = false;
    }

    void on_result() {
        execute_ready_nodes();
    }

    void initialize(InitializationInfo const &) {
        nodes_ = nullptr;
        executing_ = false;
    }

    void finalize(InitializationInfo const &) {
        // we finalize the state of the thread 0 for each node
        for (auto &node : *nodes_) {
            node->execute(make_execution_info(ExecutionInfo::Finalize));
        }
        nodes_ = nullptr;
    }
};

} // end namespace hh

#endif
