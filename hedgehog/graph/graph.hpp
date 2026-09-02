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

#ifndef HEDGEHOG_GRAPH_GRAPH_H
#define HEDGEHOG_GRAPH_GRAPH_H

#include <variant>
#include <cstdio>
#include <set>

#include "info.hpp"
#include "node.hpp"
#include "../tool/log.hpp"

namespace hh {

template <typename Config>
struct Graph : Node, NodeIO<Config> {
    using InputTypes  = Config::InputTypes;
    using OutputTypes = Config::OutputTypes;
    using Sink        = Config::Sink;
    using Executor    = Config::Executor;
    using IO          = NodeIO<Config>;

    Sink sink;
    std::set<std::shared_ptr<Node>> nodes;
    std::shared_ptr<Executor> executor;

    Graph(std::shared_ptr<Executor> executor, NodeInfo const &info): Node(info), executor(executor) {}

    void start() {
        if (IO::output().edge_count()) {
            // TODO: add the source location
            printf("error: starting a sub-graph is not allowed\n");
            return;
        }
        auto graph_info = GraphInfo{Node::info().name, 0};
        auto init_info = InitializationInfo{Node::info(), graph_info};

        // intialize the sink
        initialize_component(&sink, init_info);
        auto &graph_sink = sink;
        type_list_map<OutputTypes>([&]<typename T>() {
            IO::output().connect_edge(make_direct_edge<T>(&graph_sink));
        });

        initialize(graph_info);
        execute(ExecutionInfo{0});
    }

    void stop() {
        auto graph_info = GraphInfo{Node::info().name, 0};
        finalize(graph_info);
    }

    void initialize(GraphInfo const &graph_info) override {
        auto init_info = InitializationInfo{Node::info(), graph_info};
        IO::initialize(init_info);
        for (auto &node : nodes) {
            node->initialize(graph_info);
        }
        initialize_component(executor, init_info);
    }

    void execute(ExecutionInfo const &) override {
        for (auto &node : nodes) {
            executor->execute(node);
        }
    }

    void finalize(GraphInfo const &graph_info) override {
        auto init_info = InitializationInfo{Node::info(), graph_info};
        for (auto &node : nodes) {
            node->finalize(graph_info);
        }
        finalize_component(executor, init_info);
        IO::finalize(init_info);
        finalize_component(&sink, init_info);
    }

    //
    // TODO: it is possible to optimize the data trasfer between graph and
    //       sub-graphs by linking the connected nodes directly to the input
    //       edges:
    //       extr_node->graph_input->input_node => extr_node->input_node
    //       output_node->graph_output->extr_node => output_node->extr_node
    //

    //
    // Edge creation for a type: create an edge between 2 nodes for a specific
    // type.
    //
    // - We do not verify if nodes belong to another graph.
    // - We do not verify if the edge already exists, creating multiple edges
    //   for the same sender/receiver/type is allowed.
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
                edge(sender, receiver, create_edge.template operator()<T>(sender, receiver));
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
    void input(auto node, Edge<T> edge) {
        nodes.insert(node);
        IO::connect_input_edge(edge);
    }

    template <typename T>
    void input(auto node) {
        input(node, make_direct_edge<T>(node));
    }

    template <typename Node>
    void inputs(std::shared_ptr<Node> node, auto create_edge) {
        using node_inputs = Node::InputTypes;
        type_list_map<InputTypes>([&]<typename T>() {
            if constexpr (type_list_contains<node_inputs, T>) {
                input(node, create_edge.template operator()<T>(node));
            }
        });
    }

    template <typename Node>
    void inputs(std::shared_ptr<Node> node) {
        inputs(node, []<typename T>(auto node) {
            return make_direct_edge<T>(node);
        });
    }

    //
    // Set graph outputs.
    //

    template <typename T>
    void output(auto node, Edge<T> edge) {
        nodes.insert(node);
        node->connect_output_edge(edge);
    }

    template <typename T>
    void output(auto node) {
        output(node, [&](std::shared_ptr<T> data, RuntimeInfo const &info) {
            IO::push_result(data, info);
        });
    }

    template <typename Node>
    void outputs(std::shared_ptr<Node> node, auto create_edge) {
        using node_outputs = Node::OutputTypes;
        type_list_map<OutputTypes>([&]<typename T>() {
            if constexpr (type_list_contains<node_outputs, T>) {
                output(node, create_edge.template operator()<T>(node));
            }
        });
    }

    template <typename Node>
    void outputs(std::shared_ptr<Node> node) {
        outputs(node, [&]<typename T>(auto node) -> Edge<T> {
            return [&](std::shared_ptr<T> data, RuntimeInfo const &info) {
                IO::push_result(data, info);
            };
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
            std::make_shared<Graph<Config>>(executor->copy(), Node::info());
        }
        // TODO: all the nodes should be copied to and inputs/outputs
        return nullptr;
    }
};

} // end namespace hh

#endif
