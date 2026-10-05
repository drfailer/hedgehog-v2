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
struct TaskNode : Node, Runnable {
    using Node::initialize;
    using Runnable::initialize;
    using Node::finalize;
    using Runnable::finalize;
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
            profiler.initialize(node->info().name);
            context.construct(node, info);
            if constexpr (InitializableWith<Task, decltype(context)>) {
                task->initialize(&context);
            } else if constexpr (Initializable<Task>) {
                task->initialize();
            }
        }

        template <typename T>
        void execute(data_t<T> data) {
            using namespace std::string_literals; // for ""s
            HH_THREAD_PROFILE_REGION(profiler, "execute<"s + type_to_string<T>() + ">"s)
            {
                if constexpr (requires { Task::execute(&context, std::move(data)); }) {
                    Task::execute(&context, std::move(data));
                } else if constexpr (requires { Task::execute(std::move(data)); }) {
                    Task::execute(std::move(data));
                } else if constexpr (requires { task->execute(&context, std::move(data)); }) {
                    task->execute(&context, std::move(data));
                } else {
                    task->execute(std::move(data));
                }
            }
        }

        void finalize() {
            profiler.finalize();
            if constexpr (FinalizableWith<Task, decltype(context)>) {
                task->finalize(&context);
            } else if constexpr (Finalizable<Task>) {
                task->finalize();
            }
        }
    };

    // attributes & constructors ///////////////////////////////////////////////

    Input                    input_        = {};
    Output                   output_       = {};
    GraphInfo                graph_info_   = {};
    std::vector<ThreadState> states_       = {};

    TaskNode(std::shared_ptr<Task> task, NodeInfo const &info): Node(info), states_(info.number_threads) {
        states_[0].task = std::move(task);
    }

    Input &input() { return input_; }
    Output &output() { return output_; }
    std::vector<ThreadState> const &states() const { return states_; }
    std::shared_ptr<Task> task() { return states_[0].task; }

    void resize_threads(size_t n) {
        states_.resize(n);
        Node::number_threads(n);
    }

    // node api ////////////////////////////////////////////////////////////////

    void initialize(GraphInfo const &info) override {
        // copy the user task
        for (size_t i = 1; i < states_.size(); ++i) {
            states_[i].task = copy_component(states_[0].task);
        }
        graph_info_ = info;
        Node::profiler().initialize(Node::info().name);
        auto init_info = InitializationInfo{&Node::info(), &graph_info_, &Node::profiler()};
        input_.initialize(init_info);
        output_.initialize(init_info);
    }

    void run(RunInfo const &) override {}

    void finalize(GraphInfo const &) override {
        auto init_info = InitializationInfo{&Node::info(), &graph_info_, &Node::profiler()};
        input_.finalize(init_info);
        output_.finalize(init_info);
        Node::profiler().finalize();
    }

    void profile(ProfileMap &map) override {
        ProfileReport report;
        #ifdef HH_ENABLE_PROFILING
        report.add_profiles(Node::profiler());
        report.add_entries(merge_profiles(states_));
        #endif
        map[reinterpret_cast<uintptr_t>(this)] = std::move(report);
    }

    GraphViewNode graph_view() override {
        return GraphViewNode::make_node(this);
    }

    // Runnable interface //////////////////////////////////////////////////////

    void initialize(RuntimeInfo const &info) override {
        auto &state = states_[info.thread_index];
        RuntimeInfo ri = info;
        ri.node = &Node::info();
        ri.graph = &graph_info_;
        ri.profiler = &state.profiler;
        state.initialize(this, ri);
    }

    void finalize(RuntimeInfo const &info) override {
        states_[info.thread_index].finalize();
    }

    WaitResult wait(RuntimeInfo const &info) override {
        auto &state = states_[info.thread_index];
        WaitResult wr;
        HH_THREAD_PROFILE_REGION(state.profiler, "wait")
        {
            wr = input_.wait(state.context.info());
        }
        return wr;
    }

    size_t execute_all(RuntimeInfo const &info) override {
        auto &state = states_[info.thread_index];
        return input_.execute_all(&state, state.context.info());
    }

    bool execute_one(RuntimeInfo const &info) override {
        auto &state = states_[info.thread_index];
        return input_.execute_one(&state, state.context.info());
    }

    // io //////////////////////////////////////////////////////////////////////

    template <typename T>
    void connect_output_edge(Edge<T> edge) {
        output_.connect_edge(std::move(edge));
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        input_.push_data(std::move(data), info);
    }
};

} // end namespace hh


#endif
