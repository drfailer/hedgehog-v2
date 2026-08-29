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

#include "helpers.hpp"
#include "node.hpp"

namespace hh {

// TODO: profiling
template <typename Core>
struct TaskNode : Node {
    using Input = Core::config::node_input;
    using Output = Core::config::node_output;
    // using Profiler = Core::Profiler;

    Input input;
    Output output;
    std::vector<std::shared_ptr<Core>> cores;

    TaskNode(std::shared_ptr<Core> core, NodeInfo const &info): Node(info), cores(info.number_threads) {
        create_core_copies(cores.begin(), cores.end(), core);
    }

    void initialize() override {
        input.initialize(Node::info);
        output.initialize(Node::info);
    }

    void execute(ExecutionInfo const &info) override {
        auto core = cores[info.thread_index];

        core->task_initialize(this, info);
        for (;;) {
            auto wait_result = input.wait(info);
            if (wait_result.terminate) break;
            input.execute_consumers(core, info);
        }
        core->task_finalize(this, info);
    }

    void finalize() override {
        input.finalize(Node::info);
        output.finalize(Node::info);
    }

    std::shared_ptr<Node> copy() override {
        return std::make_shared<TaskNode<Core>>(copy_core(cores[0]), Node::info);
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
    void push_result(std::shared_ptr<T> data, ExecutionInfo const &info) {
        input.push_result(data, info);
    }
};

// functions ///////////////////////////////////////////////////////////////////

// std::shared_ptr<> make_task

} // end namespace hh


#endif
