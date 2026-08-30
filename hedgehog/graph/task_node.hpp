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

//
// The task core executes on data and contains the node configuration.
//

// TODO: profiling
template <typename Core>
struct TaskNode : Node {
    using inputs = Core::inputs;
    using outputs = Core::outputs;

    using Input = Core::config::node_input;
    using Output = Core::config::node_output;
    // using Profiler = Core::Profiler;

    Input input;
    Output output;
    std::shared_ptr<Core> core;
    std::vector<std::shared_ptr<Core>> cores;

    TaskNode(std::shared_ptr<Core> core): core(core) {}

    void initialize(NodeInfo const &info) override {
        create_core_copies(cores.begin(), cores.end(), core);
        input.initialize(info);
        output.initialize(info);
    }

    void execute(ExecutionInfo const &info) override {
        auto thread_core = cores[info.thread_index];

        thread_core->task_initialize(this, info);
        for (;;) {
            auto wait_result = input.wait(info);
            if (wait_result.terminate) break;
            input.execute_consumers(thread_core, info);
        }
        thread_core->task_finalize(this, info);
    }

    void finalize(NodeInfo const &info) override {
        input.finalize(info);
        output.finalize(info);
    }

    std::shared_ptr<Node> copy() override {
        return std::make_shared<TaskNode<Core>>(copy_core(core));
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

template <typename Core>
std::shared_ptr<TaskNode<Core>> make_task(std::shared_ptr<Core> core, size_t number_threads = 1, std::string const &name = "Task") {
    auto node = std::make_shared<TaskNode<Core>>(core);
    node->info.name = name;
    node->info.number_threads = number_threads;
    return node;
}

template <typename Core>
std::shared_ptr<TaskNode<Core>> make_task(size_t number_threads = 1, std::string const &name = "Task") {
    return make_task<Core>(std::make_shared<Core>(), number_threads, name);
}

} // end namespace hh


#endif
