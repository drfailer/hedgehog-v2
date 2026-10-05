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

#ifndef HEDGEHOG_IMPL_GRAPH_SERIAL_RUNNER_H
#define HEDGEHOG_IMPL_GRAPH_SERIAL_RUNNER_H

#include <queue>
#include <type_traits>
#include "../../graph/node.hpp"

namespace hh {

struct SerialRunner {
    std::vector<Runnable *> runnables_ = {};
    std::vector<Node *> graph_nodes_ = {};
    std::queue<int> ready_queue_;
    bool executing_ = false;
    RuntimeInfo runtime_info_ = {};

    void initialize(InitializationInfo const &) {
        executing_ = false;
    }

    template <typename ConcreteNode>
    void register_node(ConcreteNode *node) {
        if constexpr (std::is_base_of_v<Runnable, ConcreteNode>) {
            node->runner_index(static_cast<int>(runnables_.size()));
            runnables_.push_back(node);
        } else {
            graph_nodes_.push_back(node);
        }
    }

    void run(RunInfo const &info) {
        runtime_info_.run = info;
        runtime_info_.thread_index = 0;

        for (auto *node : graph_nodes_) {
            node->run(info);
        }
        for (auto *runnable : runnables_) {
            runnable->initialize(runtime_info_);
        }
    }

    void on_transfer(Runnable *runnable, RuntimeInfo const &) {
        int idx = runnable->runner_index();
        if (idx < 0) return;
        ready_queue_.push(idx);
        execute_ready();
    }

    void execute_ready() {
        if (executing_) return;
        executing_ = true;
        while (!ready_queue_.empty()) {
            auto idx = ready_queue_.front();
            ready_queue_.pop();
            runnables_[idx]->execute_all(runtime_info_);
        }
        executing_ = false;
    }

    void on_result() {
        execute_ready();
    }

    void finalize(InitializationInfo const &) {
        for (auto *runnable : runnables_) {
            runnable->finalize(runtime_info_);
        }
        runnables_.clear();
        graph_nodes_.clear();
    }
};

} // end namespace hh

#endif
