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

// NodeThreadContext ///////////////////////////////////////////////////////////

//
// Group the task and the execution context required to execute task function.
//

template <typename NodeType>
struct NodeThreadContext {
    using TaskType = NodeType::Task;
    std::shared_ptr<TaskType> task_{nullptr};
    NodeExecutionContext<NodeType> context_{};

    void task(std::shared_ptr<TaskType> task) {
        this->task_ = std::move(task);
    }

    std::shared_ptr<TaskType> task() {
        return task_;
    }

    NodeType &node() { return context_.node(); }
    RuntimeInfo const &info() { return context_.info(); }

    void initialize(NodeType *node, RuntimeInfo const &info) {
        context_.construct(node, info);
        if constexpr (requires { task_->initialize(context_); }) {
            task_->initialize(context_);
        } else if constexpr (requires { task_->initialize(); }) {
            task_->initialize();
        }
    }

    void finalize() {
        if constexpr (requires { task_->initialize(context_); }) {
            task_->finalize(context_);
        } else if constexpr (requires { task_->initialize(); }) {
            task_->finalize();
        }
    }

    void execute(auto data) {
        if constexpr (requires { task_->execute(context_, data); }) {
            auto &ctx = this->context_; // make sure we use reference
            task_->execute(ctx, data);
        } else {
            task_->execute(data);
        }
    }
};

// TaskNode ////////////////////////////////////////////////////////////////////

//
// Configurable task node implementation.
//

template <typename Config>
struct TaskNode : Node, NodeIO<Config> {
    using InputTypes  = Config::InputTypes;
    using OutputTypes = Config::OutputTypes;
    using Task        = Config::Task;
    using IO          = NodeIO<Config>;
    // TODO: using Profiler = Config::Profiler;

    GraphInfo graph_info_ = {};
    std::vector<NodeThreadContext<TaskNode<Config>>> ctxs_ = {};

    TaskNode(std::shared_ptr<Task> task, NodeInfo const &info): Node(info), ctxs_(info.number_threads) {
        for (size_t i = 1; i < info.number_threads; ++i) {
            ctxs_[i].task(copy_component(task));
        }
        ctxs_[0].task(std::move(task));
    }

    void initialize(GraphInfo const &info) override {
        graph_info_ = info;
        auto init_info = InitializationInfo{Node::info(), graph_info_};
        IO::initialize(init_info);
    }

    void execute(ExecutionInfo const &info) override {
        auto &ctx = ctxs_[info.thread_index];

        if (info.direct) {

            //
            // Direct execution used by serial or scheduled graph executor. In
            // this case, a thread will enter the function, operate the
            // executor and leave directly.
            //

            switch (info.direct_phase) {
            case ExecutionInfo::Initialize:
                ctx.initialize(this, RuntimeInfo{Node::info(), graph_info_, info});
                break;
            case ExecutionInfo::Execute:
                IO::execute(ctx, ctx.info());
                break;
            case ExecutionInfo::Finalize:
                ctx.finalize();
                break;
            }

        } else {

            //
            // Standard execution of the task. The threads are trapped in the
            // run loop until the graph terminates.
            //

            ctx.initialize(this, RuntimeInfo{Node::info(), graph_info_, info});
            for (;;) {
                auto wait_result = IO::wait(ctx.info());
                if (wait_result.terminate) break;
                if (wait_result.skip) continue;
                IO::execute(ctx, ctx.info());
            }
            ctx.finalize();
        }
    }

    void finalize(GraphInfo const &info) override {
        auto init_info = InitializationInfo{Node::info(), graph_info_};
        IO::finalize(init_info);
    }

    std::shared_ptr<Node> copy() override {
        return std::make_shared<TaskNode<Config>>(copy_component(ctxs_[0].task()), Node::info());
    }
};

} // end namespace hh


#endif
