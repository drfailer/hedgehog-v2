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

#ifndef HEDGEHOG_GRAPH_GRAPH_NODE
#define HEDGEHOG_GRAPH_GRAPH_NODE

#include <variant>
#include <set>

#include "info.hpp"
#include "node.hpp"

namespace hh {

template <typename Config>
struct GraphNode : Node, NodeIO<Config> {
    using InputTypes = Config::InputTypes;
    using OutputTypes = Config::OutputTypes;
    using IO = NodeIO<Config>;
    using Executor = Config::Executor;

    std::set<std::shared_ptr<Node>> nodes;
    std::shared_ptr<Executor> executor;

    GraphNode(std::shared_ptr<Executor> executor, NodeInfo const &info): Node(info), executor(executor) {}

    // TODO: run = initialize + execute
    // TODO: terminate = finalize

    // TODO: those should be private?

    void initialize(GraphInfo const &info) override {
        for (auto &node : nodes) {
            node->initialize(info);
        }
    }

    void execute(ExecutionInfo const &) override {
        for (auto &node : nodes) {
            executor->execute(node);
        }
    }

    void finalize(GraphInfo const &info) override {
        for (auto &node : nodes) {
            node->finalize(info);
        }
    }

    template <typename T>
    void edge(auto sender, auto receiver) {
        edge(sender, receiver, [receiver](std::shared_ptr<T> data, ExecutionInfo const &info) {
            receiver->push_data(data, info);
        });
    }

    template <typename T>
    void edge(auto sender, auto receiver, Edge<T> edge) {
        // TODO: check that the node does not belong to another graph
        nodes.insert(sender);
        nodes.insert(receiver);
        sender->connect_output_edge(edge);
        receiver->connect_input_edge(edge);
    }

    // TODO: inputs/outputs

    // TODO: get_result

    std::shared_ptr<Node> copy() override {
        if constexpr (requires { executor->copy(); }) {
            std::make_shared<GraphNode<Config>>(executor->copy(), Node::info());
        }
        return std::make_shared<GraphNode<Config>>(executor, Node::info());
    }

    // TODO: push_data

    auto get_result() {
        return IO::output.get_result();
    }
};

// functions ///////////////////////////////////////////////////////////////////

template <typename Graph>
auto make_graph(std::shared_ptr<typename Graph::Config::Executor> executor, std::string const &name = "Graph") {
    return std::make_shared<GraphNode<typename Graph::Config>>(executor, NodeInfo{name, 0});
}

template <typename Graph>
auto make_graph(std::string const &name = "Graph") {
    return make_graph<Graph>(std::make_shared<typename Graph::Config::Executor>(), name);
}

// TODO: build make the graph directly from the config?

} // end namespace hh

#endif
