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

#ifndef HEDGEHOG_GRAPH_TASK_NODE
#define HEDGEHOG_GRAPH_TASK_NODE

#include <string>
#include <memory>
#include <cassert>

#include "node.hpp"
#include "../api/node_execution_context.hpp"
#include "../tool/helpers.hpp"
#include "../tool/log.hpp"

namespace hh {

// TaskNode ////////////////////////////////////////////////////////////////////

//
// Configurable task node implementation.
//

template <typename Config>
struct TaskNode : Node, NodeIO<Config> {
    // config //////////////////////////////////////////////////////////////////

    using InputTypes  = Config::InputTypes;
    using OutputTypes = Config::OutputTypes;
    using Task        = Config::Task;
    using IO          = NodeIO<Config>;
    // TODO: using Profiler = Config::Profiler;

    // thread state ////////////////////////////////////////////////////////////

    struct ThreadState {
        std::shared_ptr<Task> task;
        NodeExecutionContext<TaskNode<Config>> context;

        void initialize(TaskNode<Config> *node, RuntimeInfo const &info) {
            context.construct(node, info);
            if constexpr (requires { task->initialize(context); }) {
                task->initialize(context);
            } else if constexpr (requires { task->initialize(); }) {
                task->initialize();
            }
        }

        void finalize() {
            if constexpr (requires { task->initialize(context); }) {
                task->finalize(context);
            } else if constexpr (requires { task->initialize(); }) {
                task->finalize();
            }
        }

        void execute(auto data) {
            if constexpr (requires { task->execute(context, data); }) {
                auto &state = this->context; // make sure we use reference
                task->execute(state, data);
            } else {
                task->execute(data);
            }
        }
    };

    // attributes & constructors ///////////////////////////////////////////////

    GraphInfo                graph_info_ = {};
    std::vector<ThreadState> states_     = {};

    TaskNode(std::shared_ptr<Task> task, NodeInfo const &info): Node(info), states_(info.number_threads) {
        for (size_t i = 1; i < info.number_threads; ++i) {
            states_[i].task = copy_component(task);
        }
        states_[0].task = std::move(task);
    }

    std::vector<ThreadState> const &states() const { return states_; } // may be usefull for some executors

    // node api ////////////////////////////////////////////////////////////////

    void initialize(GraphInfo const &info) override {
        graph_info_ = info;
        auto init_info = InitializationInfo{Node::info(), graph_info_, &Node::profiler()};
        IO::initialize(init_info);
    }

    void execute(ExecutionInfo const &info) override {
        auto &state = states_[info.thread_index];

        if (info.direct) {

            //
            // Direct execution used by serial or scheduled graph executor. In
            // this case, a thread will enter the function, operate the
            // executor and leave directly.
            //

            switch (info.direct_phase) {
            case ExecutionInfo::Initialize:
                state.initialize(this, RuntimeInfo{Node::info(), graph_info_, info});
                break;
            case ExecutionInfo::Execute:
                IO::execute(state, state.context.info());
                break;
            case ExecutionInfo::Finalize:
                state.finalize();
                break;
            }

        } else {

            //
            // Standard execution of the task. The threads are trapped in the
            // run loop until the graph terminates.
            //

            state.initialize(this, RuntimeInfo{Node::info(), graph_info_, info});
            for (;;) {
                auto wait_result = IO::wait(state.context.info());
                if (wait_result.terminate) break;
                if (wait_result.skip) continue;
                IO::execute(state, state.context.info());
            }
            state.finalize();
        }
    }

    void finalize(GraphInfo const &info) override {
        auto init_info = InitializationInfo{Node::info(), graph_info_, &Node::profiler()};
        IO::finalize(init_info);
    }
};

} // end namespace hh


#endif
