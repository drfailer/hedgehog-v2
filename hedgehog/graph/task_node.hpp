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
#include <map>

#include "node.hpp"
#include "../api/execution_context.hpp"
#include "../tool/concepts.hpp"
#include "../tool/helpers.hpp"
#include "../tool/log.hpp"

namespace hh {

// TaskNode ////////////////////////////////////////////////////////////////////

//
// Configurable task node implementation.
//

template <typename Config>
struct TaskNode : Node {
    // config //////////////////////////////////////////////////////////////////

    using InputTypes  = Config::InputTypes;
    using OutputTypes = Config::OutputTypes;
    using Task        = Config::Task;
    using Input       = Config::Input;
    using Output      = Config::Output;

    // thread state ////////////////////////////////////////////////////////////

    struct ThreadState {
        std::shared_ptr<Task> task;
        NodeExecutionContext<TaskNode<Config>> context;
        Profiler profiler;

        void initialize(TaskNode<Config> *node, RuntimeInfo const &info) {
            profiler.initialize();
            context.construct(node, info);
            if constexpr (InitializableWith<Task, decltype(context)>) {
                task->initialize(context);
            } else if constexpr (Initializable<Task>) {
                task->initialize();
            }
        }

        template <typename T>
        void execute(data_t<T> data) {
            using namespace std::string_literals; // for ""s
            HH_PROFILE_REGION(profiler, "execute<"s + type_to_string<T>() + ">"s)
            {
                if constexpr (ExecutableWithContext<Task, decltype(this->context), decltype(data)>) {
                    task->execute(&this->context, std::move(data));
                } else {
                    task->execute(std::move(data));
                }
            }
        }

        void finalize() {
            profiler.finalize();
            if constexpr (FinalizableWith<Task, decltype(context)>) {
                task->finalize(context);
            } else if constexpr (Finalizable<Task>) {
                task->finalize();
            }
        }
    };

    // attributes & constructors ///////////////////////////////////////////////

    Input                    input_      = {};
    Output                   output_     = {};
    GraphInfo                graph_info_ = {};
    std::vector<ThreadState> states_     = {};

    TaskNode(std::shared_ptr<Task> task, NodeInfo const &info): Node(info), states_(info.number_threads) {
        states_[0].task = std::move(task);
    }

    Input &input() { return input_; }
    Output &output() { return output_; }
    std::vector<ThreadState> const &states() const { return states_; } // may be usefull for some executors
    std::shared_ptr<Task> task() { return states_[0].task; } // should not be used after execute

    // node api ////////////////////////////////////////////////////////////////

    void initialize(GraphInfo const &info) override {
        // copy the user task
        for (size_t i = 1; i < states_.size(); ++i) {
            states_[i].task = copy_component(states_[0].task);
        }
        graph_info_ = info;
        Node::profiler().initialize();
        auto init_info = InitializationInfo{&Node::info(), &graph_info_, &Node::profiler()};
        input_.initialize(init_info);
        output_.initialize(init_info);
    }

    void execute(ExecutionInfo const &info) override {
        auto state = &states_[info.thread_index];

        if (info.direct) {

            //
            // Direct execution used by serial or scheduled graph executor. In
            // this case, a thread will enter the function, operate the
            // executor and leave directly.
            //

            switch (info.direct_phase) {
            case ExecutionInfo::Initialize:
                state->initialize(this, RuntimeInfo{&Node::info(), &graph_info_, info, &state->profiler});
                break;
            case ExecutionInfo::Execute:
                input_.execute(state, state->context.info());
                break;
            case ExecutionInfo::Finalize:
                state->finalize();
                break;
            }

        } else {

            //
            // Standard execution of the task. The threads are trapped in the
            // run loop until the graph terminates.
            //

            state->initialize(this, RuntimeInfo{&Node::info(), &graph_info_, info, &state->profiler});
            WaitResult wait_result;
            for (;;) {
                HH_PROFILE_REGION(state->profiler, "wait")
                {
                    wait_result = input_.wait(state->context.info());
                }
                if (wait_result.terminate) break;
                if (wait_result.skip) continue;
                input_.execute(state, state->context.info());
            }
            state->finalize();
        }
    }

    void finalize(GraphInfo const &) override {
        auto init_info = InitializationInfo{&Node::info(), &graph_info_, &Node::profiler()};
        input_.finalize(init_info);
        output_.finalize(init_info);
        Node::profiler().finalize();
    }

    ProfilerReport profile() override {
        ProfilerReport report = Node::profiler().create_report(Node::info().name, ProfileReportKind::Node);
        report.id = reinterpret_cast<uintptr_t>(static_cast<Node *>(this));
#ifdef HH_ENABLE_PROFILING
        for (auto &state : states_) {
            for (auto &[label, profile] : state.profiler.profiles) {
                report.add_profile(label, *profile.get());
            }
        }
#endif
        return report;
    }

    // io //////////////////////////////////////////////////////////////////////

    template <typename T>
    void connect_output_edge(Edge<T> edge) {
        output_.connect_edge(std::move(edge));
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info = {}) {
        input_.push_data(std::move(data), info);
    }
};

} // end namespace hh


#endif
