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
    // config //////////////////////////////////////////////////////////////////

    using InputTypes  = Config::InputTypes;
    using OutputTypes = Config::OutputTypes;
    using Sink        = Config::Sink;
    using Executor    = Config::Executor;
    using IO          = NodeIO<Config>;

    // attributes & constructors ///////////////////////////////////////////////

    Sink sink_;
    std::set<std::shared_ptr<Node>> nodes_;
    std::shared_ptr<Executor> executor_;

    Graph(std::shared_ptr<Executor> executor_, NodeInfo const &info): Node(info), executor_(executor_) {}

    // user functions //////////////////////////////////////////////////////////

    void start() {
        if (IO::output().edge_count()) {
            // TODO: add the source location
            printf("error: starting a sub-graph is not allowed\n");
            return;
        }
        auto graph_info = GraphInfo{Node::info().name, 0};
        auto init_info = InitializationInfo{Node::info(), graph_info};

        // intialize the sink_
        initialize_component(&sink_, init_info);
        auto &graph_sink = sink_;
        type_list_map<OutputTypes>([&]<typename T>() {
            IO::output().connect_edge(make_direct_edge<T>(executor_, &graph_sink));
        });
        // TODO: the executor_ may need to use the sink_ as well

        initialize(graph_info);
        execute(ExecutionInfo{0});
    }

    void stop() {
        auto graph_info = GraphInfo{Node::info().name, 0};
        finalize(graph_info);
    }

    template <typename T>
    void push_data(std::shared_ptr<T> data) {
        IO::push_data(data, {});
    }

    auto get_result() {
        if constexpr (requires { executor_->on_result(); }) {
            executor_->on_result(); // important for the serial executor_
        }
        return sink_.get_result();
    }

    void eat_resutls(size_t count = 1) {
        for (size_t i = 0; i < count; ++i) {
            auto _ = get_result();
        }
    }

    // node api ////////////////////////////////////////////////////////////////

    void initialize(GraphInfo const &graph_info) override {
        auto init_info = InitializationInfo{Node::info(), graph_info};
        IO::initialize(init_info);
        for (auto &node : nodes_) {
            node->initialize(graph_info);
        }
        initialize_component(executor_, init_info);
    }

    void execute(ExecutionInfo const &) override {
        for (auto &node : nodes_) {
            executor_->execute(node);
        }
    }

    void finalize(GraphInfo const &graph_info) override {
        auto init_info = InitializationInfo{Node::info(), graph_info};
        for (auto &node : nodes_) {
            node->finalize(graph_info);
        }
        finalize_component(executor_, init_info);
        IO::finalize(init_info);
        finalize_component(&sink_, init_info);
    }

    // edges ///////////////////////////////////////////////////////////////////

    //
    // TODO: it is possible to optimize the data trasfer between graph and
    //       sub-graphs by linking the connected nodes_ directly to the input
    //       edges:
    //       extr_node->graph_input->input_node => extr_node->input_node
    //       output_node->graph_output->extr_node => output_node->extr_node
    //

    //
    // Edge creation for a type: create an edge between 2 nodes_ for a specific
    // type.
    //
    // - We do not verify if nodes_ belong to another graph.
    // - We do not verify if the edge already exists, creating multiple edges
    //   for the same sender/receiver/type is allowed.
    //

    template <typename T>
    void draw_edge(auto sender, auto receiver, Edge<T> edge) {
        nodes_.insert(sender);
        nodes_.insert(receiver);
        sender->connect_output_edge(edge);
        receiver->connect_input_edge(edge);
    }

    template <typename T>
    void draw_edge(auto sender, auto receiver) {
        draw_edge(sender, receiver, make_direct_edge<T>(executor_, receiver));
    }

    //
    // Edge creation for common types: create an edge between 2 nodes_ for every
    // types common between the sender outputs and receiver inputs.
    //
    // - We do not verify if nodes_ belong to another graph.
    // - We do not verify if the edges already exist, creating multiple edges
    //   for the same sender/receiver/type is allowed.
    //

    template <typename Sender, typename Receiver>
    void draw_edges(std::shared_ptr<Sender> sender, std::shared_ptr<Receiver> receiver, auto create_edge) {
        using sender_outputs = Sender::OutputTypes;
        using receiver_inputs = Sender::InputTypes;
        type_list_map<sender_outputs>([&]<typename T>() {
            if constexpr (type_list_contains<receiver_inputs, T>) {
                draw_edge(sender, receiver, create_edge.template operator()<T>(sender, receiver));
            }
        });
    }

    void draw_edges(auto sender, auto receiver) {
        draw_edges(sender, receiver, [&]<typename T>(auto sender, auto receiver) {
            return make_direct_edge<T>(executor_, receiver);
        });
    }

    // inputs & outputs ////////////////////////////////////////////////////////

    //
    // Set graph inputs.
    //

    template <typename T>
    void connect_input(auto node, Edge<T> edge) {
        nodes_.insert(node);
        IO::connect_input_edge(edge);
    }

    template <typename T>
    void connect_input(auto node) {
        connect_input(node, make_direct_edge<T>(executor_, node));
    }

    template <typename Node>
    void connect_inputs(std::shared_ptr<Node> node, auto create_edge) {
        using node_inputs = Node::InputTypes;
        type_list_map<InputTypes>([&]<typename T>() {
            if constexpr (type_list_contains<node_inputs, T>) {
                connect_input(node, create_edge.template operator()<T>(node));
            }
        });
    }

    template <typename Node>
    void connect_inputs(std::shared_ptr<Node> node) {
        connect_inputs(node, [&]<typename T>(auto node) {
            return make_direct_edge<T>(executor_, node);
        });
    }

    //
    // Set graph outputs.
    //

    template <typename T>
    void connect_output(auto node, Edge<T> edge) {
        nodes_.insert(node);
        node->connect_output_edge(edge);
    }

    template <typename T>
    void connect_output(auto node) {
        connect_output(node, [&](std::shared_ptr<T> data, RuntimeInfo const &info) {
            IO::push_result(data, info);
        });
    }

    template <typename Node>
    void connect_outputs(std::shared_ptr<Node> node, auto create_edge) {
        using node_outputs = Node::OutputTypes;
        type_list_map<OutputTypes>([&]<typename T>() {
            if constexpr (type_list_contains<node_outputs, T>) {
                connect_output(node, create_edge.template operator()<T>(node));
            }
        });
    }

    template <typename Node>
    void connect_outputs(std::shared_ptr<Node> node) {
        connect_outputs(node, [&]<typename T>(auto node) -> Edge<T> {
            return [&](std::shared_ptr<T> data, RuntimeInfo const &info) {
                IO::push_result(data, info);
            };
        });
    }
};

} // end namespace hh

#endif
