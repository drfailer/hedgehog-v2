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
#include <sstream>

#include <fstream>

#include "info.hpp"
#include "node.hpp"
#include "io.hpp"
#include "edge.hpp"
#include "../tool/concepts.hpp"
#include "../tool/log.hpp"
#include "../tool/profiling_report.hpp"
#include "../impl/graph/graph_input.hpp"

namespace hh {

template <typename Config>
struct Graph : Node, NodeIO<Config> {
    // config //////////////////////////////////////////////////////////////////

    using InputTypes  = Config::InputTypes;
    using OutputTypes = Config::OutputTypes;
    using Sink        = Config::Sink;
    using Executor    = Config::Executor;
    using EdgeBuilder = Config::EdgeBuilder;
    using IO          = NodeIO<Config>;

    //
    // Argument struct to pass to the edge builder. We use a struct because it
    // makes things easier to change. Note that this struct stores standard
    // pointers and not shared pointers. This is important because the edges are
    // heavily used by multiple threads during the execution, and we don't want
    // them to go through the shared_ptr api which could introduce extra atomic
    // operations (the lifetime of all the arguments is at least the same as
    // the graph, so the data will always be available to the edge).
    //
    template <typename Sender, typename Receiver>
    struct MakeEdgeArgs {
        Sender *sender;
        Receiver *receiver;
        Graph<Config> *graph;
    };

    // attributes & constructors ///////////////////////////////////////////////

    std::shared_ptr<Executor> executor_;
    std::shared_ptr<EdgeBuilder> edge_builder_;
    Sink sink_;
    std::set<std::shared_ptr<Node>> nodes_;
    std::set<std::shared_ptr<Node>> input_nodes_;
    std::set<std::shared_ptr<Node>> output_nodes_;
    std::vector<Connection> connections_;

    struct IOConnection {
        Node *node;
        std::string type_name;
    };
    std::vector<IOConnection> input_connections_;
    std::vector<IOConnection> output_connections_;

#ifdef HH_ENABLE_PROFILING
    Profile *exec_profile_ = nullptr;
#endif

    Graph(std::shared_ptr<Executor>     executor,
          std::shared_ptr<EdgeBuilder>  edge_builder,
          NodeInfo const               &info)
        : Node(info),
          executor_(std::move(executor)),
          edge_builder_(std::move(edge_builder)) {
    }

    std::shared_ptr<Executor> executor() const {  return executor_; }
    std::shared_ptr<EdgeBuilder> edge_builder() const { return edge_builder_; }
    Sink const &sink() const { return sink_; }
    std::set<std::shared_ptr<Node>> const &nodes() const { return nodes_; }
    std::set<std::shared_ptr<Node>> const &input_nodes() const { return input_nodes_; }
    std::set<std::shared_ptr<Node>> const &output_nodes() const { return output_nodes_; }
    std::vector<Connection> const &connections() const { return connections_; }

    // user functions //////////////////////////////////////////////////////////

    void start() {
        if (IO::output().edge_count()) {
            // TODO: add the source location
            printf("error: starting a sub-graph is not allowed\n");
            return;
        }
        auto graph_info = GraphInfo{Node::info().name, 0};

        // intialize the sink_
        sink_.initialize(InitializationInfo{Node::info(), graph_info, nullptr});
        auto &graph_sink = sink_;
        type_list_map<OutputTypes>([&]<typename T>() {
            IO::output().connect_edge(make_edge<T>(&graph_sink));
        });

        initialize(graph_info);
#ifdef HH_ENABLE_PROFILING
        exec_profile_ = Node::profiler().create_profile("execution");
        exec_profile_->begin_region();
#endif
        execute(ExecutionInfo{0});
    }

    void stop() {
#ifdef HH_ENABLE_PROFILING
        exec_profile_->end_region();
        auto *fin_profile = Node::profiler().create_profile("finalization");
        fin_profile->begin_region();
#endif
        finalize(GraphInfo{Node::info().name, 0});
#ifdef HH_ENABLE_PROFILING
        fin_profile->end_region();
#endif
    }

    template <typename T>
    void push_data(data_t<T> data) {
        IO::push_data(std::move(data), {});
    }

    auto get_result() {
        if constexpr (HasOnResult<Executor>) {
            executor_->on_result();
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
        auto init_info = InitializationInfo{Node::info(), graph_info, &Node::profiler()};
        Node::profiler().initialize();

        auto *init_profile = Node::profiler().create_profile("initialize");
        init_profile->begin_region();
        IO::initialize(init_info);
        for (auto &node : nodes_) {
            node->initialize(graph_info);
        }
        initialize_component(executor_, init_info);
        init_profile->end_region();
    }

    void execute(ExecutionInfo const &) override {
        for (auto &node : nodes_) {
            executor_->execute(node.get());
        }
    }

    void finalize(GraphInfo const &graph_info) override {
        auto init_info = InitializationInfo{Node::info(), graph_info, &Node::profiler()};
        for (auto &node : nodes_) {
            node->finalize(graph_info);
        }
        executor_->finalize(init_info);
        IO::finalize(init_info);
        sink_.finalize(init_info);
        Node::profiler().finalize();
    }

    ProfilerReport profile() override {
        ProfilerReport report = Node::profiler().create_report(Node::info().name, ProfileReportKind::Graph);
        report.id = reinterpret_cast<uintptr_t>(static_cast<Node *>(this));
        for (auto &conn : input_connections_) {
            report.input_edges.push_back({conn.type_name, reinterpret_cast<uintptr_t>(conn.node)});
        }
        for (auto &conn : output_connections_) {
            report.output_edges.push_back({conn.type_name, reinterpret_cast<uintptr_t>(conn.node)});
        }
        for (auto &node : nodes_) {
            report.add_report(node->profile());
        }
        return report;
    }

    // edges ///////////////////////////////////////////////////////////////////

    //
    // Input-side edge flattening: draw_edge<T> and connect_input<T> detect
    // when the receiver/node is a Graph and bypass GraphInput by reusing inner
    // edge transfer functions. Output-side flattening is not yet implemented.
    //

    template <typename T>
    Edge<T> make_edge(auto sender, auto receiver) {
        return edge_builder_->template make_edge<T>(MakeEdgeArgs{
            .sender = sender,
            .receiver = receiver,
            .graph = this,
        });
    }

    template <typename T>
    Edge<T> make_edge(auto receiver) {
        return make_edge<T>((void *)nullptr, receiver);
    }

    template <typename T>
    Edge<T> make_output_edge() {
        return Edge<T>(this, [](Edge<T> *e, data_t<T> data, RuntimeInfo const &info) {
            static_cast<Graph<Config> *>(e->graph)->template push_result<T>(std::move(data), info);
        });
    }

    //
    // Edge creation for a type: create an edge between 2 nodes_ for a specific
    // type.
    //
    // - We do not verify if nodes_ belong to another graph.
    // - We do not verify if the edge already exists, creating multiple edges
    //   for the same sender/receiver/type is allowed.
    // - Sub-graphs input edges are not flattened when a custom edge is used.
    // - Sub-graphs output edges are not flattened.
    //

    template <typename T>
    void draw_edge(auto sender, auto receiver, Edge<T> edge) {
        nodes_.insert(sender);
        nodes_.insert(receiver);
        connections_.push_back(Connection{sender.get(), receiver.get(), type_to_string<T>()});
        receiver->connect_input_edge(edge);
        sender->connect_output_edge(std::move(edge));
    }

    template <typename T>
    void draw_edge(auto sender, auto receiver) {
        if constexpr (HasInputNodes<typename decltype(receiver)::element_type>) {
            nodes_.insert(sender);
            nodes_.insert(receiver);
            auto& inner_edges = static_cast<GraphInputPort<T>&>(receiver->input()).edges;
            for (auto& inner_edge : inner_edges) {
                Edge<T> flat(sender.get(), inner_edge.receiver, inner_edge.graph, inner_edge.fun);
                connections_.push_back(Connection{sender.get(), receiver.get(), type_to_string<T>()});
                sender->connect_output_edge(std::move(flat));
            }
        } else {
            draw_edge(sender, receiver, make_edge<T>(sender.get(), receiver.get()));
        }
    }

    //
    // Edge creation for common types: create an edge between 2 nodes_ for every
    // types common between the sender outputs and receiver inputs.
    //
    // - We do not verify if nodes_ belong to another graph.
    // - We do not verify if the edges already exist, creating multiple edges
    //   for the same sender/receiver/type is allowed.
    // - Sub-graphs input edges are not flattened when a custom edge is used.
    // - Sub-graphs output edges are not flattened.
    //

    template <typename Sender, typename Receiver>
    void draw_edges(std::shared_ptr<Sender> sender, std::shared_ptr<Receiver> receiver, auto create_edge) {
        using sender_outputs = Sender::OutputTypes;
        using receiver_inputs = Receiver::InputTypes;
        type_list_map<sender_outputs>([&]<typename T>() {
            if constexpr (type_list_contains<receiver_inputs, T>) {
                draw_edge(sender, receiver, create_edge.template operator()<T>(sender, receiver));
            }
        });
    }

    template <typename Sender, typename Receiver>
    void draw_edges(std::shared_ptr<Sender> sender, std::shared_ptr<Receiver> receiver) {
        using sender_outputs = Sender::OutputTypes;
        using receiver_inputs = Receiver::InputTypes;
        type_list_map<sender_outputs>([&]<typename T>() {
            if constexpr (type_list_contains<receiver_inputs, T>) {
                draw_edge<T>(sender, receiver);
            }
        });
    }

    // inputs & outputs ////////////////////////////////////////////////////////

    //
    // Set graph inputs.
    //

    template <typename T>
    void connect_input(auto node, Edge<T> edge) {
        nodes_.insert(node);
        input_nodes_.insert(node);
        input_connections_.push_back({node.get(), type_to_string<T>()});
        IO::connect_input_edge(std::move(edge));
    }

    template <typename T>
    void connect_input(auto node) {
        nodes_.insert(node);
        input_nodes_.insert(node);
        input_connections_.push_back({node.get(), type_to_string<T>()});
        if constexpr (HasInputNodes<typename decltype(node)::element_type>) {
            auto& inner_edges = static_cast<GraphInputPort<T>&>(node->input()).edges;
            for (auto& inner_edge : inner_edges) {
                Edge<T> forwarded(nullptr, inner_edge.receiver, inner_edge.graph, inner_edge.fun);
                IO::connect_input_edge(std::move(forwarded));
            }
        } else {
            IO::connect_input_edge(make_edge<T>(node.get()));
        }
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
        using node_inputs = Node::InputTypes;
        type_list_map<InputTypes>([&]<typename T>() {
            if constexpr (type_list_contains<node_inputs, T>) {
                connect_input<T>(node);
            }
        });
    }

    //
    // Set graph outputs.
    //

    template <typename T>
    void connect_output(auto node, Edge<T> edge) {
        nodes_.insert(node);
        output_nodes_.insert(node);
        output_connections_.push_back({node.get(), type_to_string<T>()});
        node->connect_output_edge(std::move(edge));
    }

    template <typename T>
    void connect_output(auto node) {
        connect_output(node, make_output_edge<T>());
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
            return make_output_edge<T>();
        });
    }

    // profiling ///////////////////////////////////////////////////////////////

    void generate_dot_file(std::string const &filename) {
        auto report = this->profile();
        std::ofstream ofs(filename);
        report_to_dot(report, ofs);
    }
};

} // end namespace hh

#endif
