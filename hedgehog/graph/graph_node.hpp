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
    using InputTypes  = Config::InputTypes;
    using OutputTypes = Config::OutputTypes;
    using Sink        = Config::Sink;
    using Executor    = Config::Executor;
    using IO          = NodeIO<Config>;

    Sink sink;
    std::set<std::shared_ptr<Node>> nodes;
    std::shared_ptr<Executor> executor;

    GraphNode(std::shared_ptr<Executor> executor, NodeInfo const &info): Node(info), executor(executor) {}

    void start() {
        initialize(GraphInfo{Node::info().name, 0, 0});
        execute(ExecutionInfo{0});
        if (IO::output.edge_count()) {
            auto &output = IO::output;
            auto &graph_sink = sink;
            type_list_map<OutputTypes>([&]<typename T>() {
                output.connect_edge(make_direct_edge<T>(&graph_sink));
            });
        }
    }

    void stop() {
        finalize(GraphInfo{Node::info().name, 0, 0});
    }

    void initialize(GraphInfo const &info) override {
        IO::initialize(Node::info());
        initialize_component(&sink, Node::info());
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
        finalize_component(&sink, Node::info());
        IO::finalize(Node::info());
    }

    //
    // Edge creation for a type: create an edge between 2 nodes for a specific
    // type.
    //
    // - We do not verify if nodes belong to another graph.
    // - We do not verify if the edge already exists, creating multiple edges
    //   for the same sender/receiver/type is allowed.
    //
    // TODO: we may use "if constexpr" to display a simpler error message at runtime
    //

    template <typename T>
    void edge(auto sender, auto receiver, Edge<T> edge) {
        nodes.insert(sender);
        nodes.insert(receiver);
        sender->connect_output_edge(edge);
        receiver->connect_input_edge(edge);
    }

    template <typename T>
    void edge(auto sender, auto receiver) {
        edge(sender, receiver, make_direct_edge<T>(receiver));
    }

    //
    // Edge creation for common types: create an edge between 2 nodes for every
    // types common between the sender outputs and receiver inputs.
    //
    // - We do not verify if nodes belong to another graph.
    // - We do not verify if the edges already exist, creating multiple edges
    //   for the same sender/receiver/type is allowed.
    //

    template <typename Sender, typename Receiver>
    void edges(std::shared_ptr<Sender> sender, std::shared_ptr<Receiver> receiver, auto create_edge) {
        using sender_outputs = Sender::OutputTypes;
        using receiver_inputs = Sender::InputTypes;
        type_list_map<sender_outputs>([&]<typename T>() {
            if constexpr (type_list_contains<receiver_inputs, T>) {
                auto edge = create_edge.template operator()<T>(sender, receiver);
                sender->connect_output_edge(edge);
                receiver->connect_input_edge(edge);
            }
        });
    }

    void edges(auto sender, auto receiver) {
        edges(sender, receiver, []<typename T>(auto sender, auto receiver) {
            return make_direct_edge<T>(receiver);
        });
    }

    //
    // Set graph inputs.
    //

    template <typename T>
    void input(auto node) {
        IO::connect_input_edge(make_direct_edge<T>(node));
    }

    template <typename Node>
    void inputs(std::shared_ptr<Node> node) {
        using node_inputs = Node::InputTypes;
        auto &input = IO::input; // can't capture this in generic lambda
        type_list_map<InputTypes>([&]<typename T>() {
            if constexpr (type_list_contains<node_inputs, T>) {
                input.connect_edge(make_direct_edge<T>(node));
            }
        });
    }

    //
    // Set graph outputs.
    //

    template <typename T>
    void output(auto node) {
        auto &output = IO::output;
        node->connect_output_edge([&](std::shared_ptr<T> data, ExecutionInfo const &info) {
            output.push_result(data, info);
        });
    }

    template <typename Node>
    void outputs(std::shared_ptr<Node> node, auto create_edge) {
        using node_outputs = Node::OutputTypes;
        auto &output = IO::output; // can't capture this in generic lambda
        type_list_map<OutputTypes>([&]<typename T>() {
            if constexpr (type_list_contains<node_outputs, T>) {
                output.connect_edge(make_direct_edge<T>(node));
            }
        });
    }

    template <typename Node>
    void outputs(std::shared_ptr<Node> node) {
        outputs(node, []<typename T>(auto node) {
            return make_direct_edge<T>(node);
        });
    }

    template <typename T>
    void push_data(std::shared_ptr<T> data) {
        IO::push_data(data, {});
    }

    auto get_result() {
        return sink.get_result();
    }

    //
    // Copy the graph for pipelines.
    //

    std::shared_ptr<Node> copy() override {
        if constexpr (requires { executor->copy(); }) {
            std::make_shared<GraphNode<Config>>(executor->copy(), Node::info());
            // TODO: all the nodes should be copied to and inputs/outputs
        }
        return std::make_shared<GraphNode<Config>>(executor, Node::info());
    }
};

} // end namespace hh

#endif
