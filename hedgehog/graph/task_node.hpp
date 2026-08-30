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

#include "helpers.hpp"
#include "node.hpp"

namespace hh {

template <typename Config>
struct TaskNode : Node {
    using InputTypes = Config::InputTypes;
    using OutputTypes = Config::OutputTypes;
    using Input = Config::Input;
    using Output = Config::Output;
    using Task = Config::Task;
    // TODO: using Profiler = Config::Profiler;

    GraphInfo graph_info;
    Input input;
    Output output;
    std::vector<std::shared_ptr<Task>> tasks;

    TaskNode(std::shared_ptr<Task> task, NodeInfo const &info): Node(info), tasks(info.number_threads) {
        create_task_copies(tasks.begin(), tasks.end(), task);
    }

    void initialize(GraphInfo const &info) override {
        graph_info = info;
        input.initialize(Node::info());
        output.initialize(Node::info());
        for (auto &task : tasks) {
            task->set_node(this);
            if constexpr (requires { task->initialize(); }) {
                task->initialize();
            }
        }
    }

    void execute(ExecutionInfo const &info) override {
        auto thread_task = tasks[info.thread_index];

        thread_task->set_execution_info(info);
        for (;;) {
            auto wait_result = input.wait(info);
            if (wait_result.terminate) break;
            input.execute_consumers(thread_task, info);
        }
    }

    void finalize(GraphInfo const &info) override {
        input.finalize(Node::info());
        output.finalize(Node::info());
        for (auto &task : tasks) {
            if constexpr (requires { task->finalize(); }) {
                task->finalize();
            }
        }
    }

    std::shared_ptr<Node> copy() override {
        return std::make_shared<TaskNode<Config>>(copy_task(tasks[0]), Node::info());
    }

    template <typename T>
    void connect_input_edge(Edge<T> edge) {
        input.connect_edge(std::move(edge));
    }

    template <typename T>
    void connect_output_edge(Edge<T> edge) {
        output.connect_edge(std::move(edge));
    }

    template <typename T>
    void push_data(std::shared_ptr<T> data, ExecutionInfo const &info) {
        input.push_data(data, info);
    }

    template <typename T>
    void push_result(std::shared_ptr<T> data, ExecutionInfo const &info) {
        output.push_result(data, info);
    }
};

// functions ///////////////////////////////////////////////////////////////////

template <typename Task>
auto make_task(std::shared_ptr<Task> task, size_t number_threads = 1, std::string const &name = "Task") {
    return std::make_shared<TaskNode<typename Task::Config>>(task, NodeInfo{name, number_threads});
}

template <typename Task>
auto make_task(size_t number_threads = 1, std::string const &name = "Task") {
    return make_task(std::make_shared<Task>(), number_threads, name);
}

} // end namespace hh


#endif
