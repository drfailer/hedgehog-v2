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

#include <vector>
#include <queue>
#include <thread>
#include <cstdio>
#include "../../graph/node.hpp"

namespace hh {

struct SerialExecutor {
    std::vector<std::shared_ptr<Node>> nodes;
    std::queue<std::shared_ptr<Node>> ready_nodes;

    ExecutionInfo make_execution_info(auto phase) {
        return ExecutionInfo{
            .thread_index = 0,
            .rank = 0,
            .numa_id = 0,
            .device_id = 0,
            .direct = true,
            .direct_phase = phase,
        };
    }

    void execute(std::shared_ptr<Node> node) {
        // we register the nodes and initialize the state of the thread 0
        nodes.push_back(node);
        node->execute(make_execution_info(ExecutionInfo::Initialize));
    }

    void on_push_data(std::shared_ptr<Node> node, RuntimeInfo const &) {
        // we don't execute the node directly here, otherwize cyclic graphs
        // with a lot of data would stack overflow.
        ready_nodes.push(node);
    }

    void on_get_result() {
        // the graph execution is triggered in get_result (we make sure the
        // graph executes before trying to pop the sink).
        while (!ready_nodes.empty()) {
            auto node = ready_nodes.front();
            ready_nodes.pop();
            node->execute(make_execution_info(ExecutionInfo::Execute));
        }
    }

    void finalize() {
        // we finalize the state of the thread 0 for each node
        for (auto node : nodes) {
            node->execute(make_execution_info(ExecutionInfo::Finalize));
        }
    }
};

} // end namespace hh

#endif
