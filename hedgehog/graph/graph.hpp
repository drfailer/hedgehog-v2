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

namespace hh {

// Graph ///////////////////////////////////////////////////////////////////////

template <typename Config>
struct Graph : Node {
    // config //////////////////////////////////////////////////////////////////

    using InputTypes  = Config::InputTypes;
    using OutputTypes = Config::OutputTypes;
    using Sink        = Config::Sink;
    using Executor    = Config::Executor;
    using Input       = type_list_dispatch<InputTypes, EdgeSlots>;
    using Output      = type_list_dispatch<OutputTypes, EdgeSlots>;

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

    static inline int graph_id_gen_ = 0;

    GraphInfo graph_info_;
    Input input_   = {};
    Output output_ = {};
    std::shared_ptr<Executor> executor_;
    Sink sink_;
    std::set<std::shared_ptr<Node>> nodes_;
    std::set<std::shared_ptr<Node>> input_nodes_;
    std::set<std::shared_ptr<Node>> output_nodes_;
    std::vector<Connection> connections_;

#ifdef HH_ENABLE_PROFILING
    Profile *exec_profile_ = nullptr;
#endif

    Graph(std::shared_ptr<Executor>     executor,
          NodeInfo const               &info)
        : Node(info),
          executor_(std::move(executor)) {
        graph_info_.name = info.name;
        graph_info_.id = graph_id_gen_++;
    }

    Input &input() { return input_; }
    Output &output() { return output_; }
    std::shared_ptr<Executor> executor() const {  return executor_; }
    Sink const &sink() const { return sink_; }
    std::set<std::shared_ptr<Node>> const &nodes() const { return nodes_; }
    std::set<std::shared_ptr<Node>> const &input_nodes() const { return input_nodes_; }
    std::set<std::shared_ptr<Node>> const &output_nodes() const { return output_nodes_; }
    std::vector<Connection> const &connections() const { return connections_; }

    // user functions //////////////////////////////////////////////////////////

    void start(int rank = 0) {
        if (output_.edge_count()) {
            // TODO: add the source location
            printf("error: starting a sub-graph is not allowed\n");
            return;
        }

        // intialize the sink_
        sink_.initialize(InitializationInfo{&Node::info(), &graph_info_, nullptr});
        auto &graph_sink = sink_;
        type_list_map<OutputTypes>([&]<typename T>() {
            output_.connect_edge(Edge<T>(this, [](Edge<T> *e, data_t<T> data, RuntimeInfo const &info) {
                static_cast<decltype(this)>(e->graph)->sink_.push_data(data, info);
            }));
        });

        initialize(graph_info_);
#ifdef HH_ENABLE_PROFILING
        exec_profile_ = Node::profiler().profile("execution");
        exec_profile_->begin_region();
#endif
        ExecutionInfo exec_info = {0};
        exec_info.rank = rank;
        execute(exec_info);
    }

    void stop() {
#ifdef HH_ENABLE_PROFILING
        exec_profile_->end_region();
        auto *fin_profile = Node::profiler().profile("finalization");
        fin_profile->begin_region();
#endif
        finalize(GraphInfo{Node::info().name, 0});
#ifdef HH_ENABLE_PROFILING
        fin_profile->end_region();
#endif
    }

    template <typename T>
    void connect_output_edge(Edge<T> edge) {
        output_.connect_edge(std::move(edge));
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info = {}) {
        input_.push_data(std::move(data), info);
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

    void initialize(GraphInfo const &) override {
        auto init_info = InitializationInfo{&Node::info(), &graph_info_, &Node::profiler()};
        Node::profiler().initialize();

        auto *init_profile = Node::profiler().profile("initialize");
        init_profile->begin_region();
        input_.initialize(init_info);
        output_.initialize(init_info);
        for (auto &node : nodes_) {
            node->initialize(graph_info_);
        }
        executor_->initialize(init_info);
        init_profile->end_region();
    }

    void execute(ExecutionInfo const &info) override {
        executor_->execute(nodes_, info);
    }

    void finalize(GraphInfo const &) override {
        auto init_info = InitializationInfo{&Node::info(), &graph_info_, &Node::profiler()};
        for (auto &node : nodes_) {
            node->finalize(graph_info_);
        }
        executor_->finalize(init_info);
        input_.finalize(init_info);
        output_.finalize(init_info);
        sink_.finalize(init_info);
        Node::profiler().finalize();
    }

    ProfilerReport profile() override {
        ProfilerReport report = Node::profiler().create_report(Node::info().name, ProfileReportKind::Graph);
        report.id = reinterpret_cast<uintptr_t>(static_cast<Node *>(this));
        for (auto &node : nodes_) {
            report.add_report(node->profile());
        }
        uintptr_t edge_id = 0;
        for (auto conn : connections_) {
            report.add_report(ProfilerReport(edge_id++,
                                             reinterpret_cast<uintptr_t>(conn.sender),
                                             reinterpret_cast<uintptr_t>(conn.receiver),
                                             conn.type_name));
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
    Edge<T> make_edge(auto receiver) {
        using Receiver = std::remove_pointer_t<decltype(receiver)>;
        return Edge<T>(receiver, this, [](Edge<T> *e, data_t<T> data, RuntimeInfo const &info) {
            auto receiver = static_cast<Receiver *>(e->receiver);
            auto graph = static_cast<Graph *>(e->graph);
            receiver->push_data(std::move(data), info);
            if constexpr (HasOnTransfer<Executor, Receiver, RuntimeInfo>) {
                graph->executor()->on_transfer(receiver, info);
            }
        });
    }

    // TODO: output edges should be edge builder / edge setup functions that
    //       connects a given edge to the captured sender
    //       ex:
    //       - sub_graph_output_node.connect_edge(edge) (edge sends to outer receiver)
    //       - graph_output.connect_edge(edge) (edge sends to sink)
    // TODO: Maybe the graph output should not be an edge slot, but an edge
    //       connector that connects a given edge to node outputs
    template <typename T>
    Edge<T> make_output_edge() {
        using GraphType = decltype(this);
        return Edge<T>(this, [](Edge<T> *e, data_t<T> data, RuntimeInfo const &info) {
            static_cast<GraphType>(e->graph)->output().template push_data<T>(std::move(data), info);
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
        sender->connect_output_edge(std::move(edge));
    }

    template <typename T>
    void draw_edge(auto sender, auto receiver) {
        if constexpr (HasInputNodes<typename decltype(receiver)::element_type>) {
            nodes_.insert(sender);
            nodes_.insert(receiver);
            auto& inner_edges = receiver->input().template edges<T>();
            for (auto& inner_edge : inner_edges) {
                Edge<T> flat(inner_edge.receiver, inner_edge.graph, inner_edge.fun);
                connections_.push_back(Connection{sender.get(), receiver.get(), type_to_string<T>()});
                sender->connect_output_edge(std::move(flat));
            }
        } else {
            draw_edge(sender, receiver, make_edge<T>(receiver.get()));
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
        connections_.push_back(Connection{nullptr, node.get(), type_to_string<T>()});
        input_.connect_edge(std::move(edge));
    }

    template <typename T>
    void connect_input(auto node) {
        nodes_.insert(node);
        input_nodes_.insert(node);
        connections_.push_back(Connection{nullptr, node.get(), type_to_string<T>()});
        if constexpr (HasInputNodes<typename decltype(node)::element_type>) {
            auto& inner_edges = node->input().template edges<T>();
            for (auto& inner_edge : inner_edges) {
                Edge<T> forwarded(nullptr, inner_edge.receiver, inner_edge.graph, inner_edge.fun);
                input_.connect_edge(std::move(forwarded));
            }
        } else {
            input_.connect_edge(make_edge<T>(node.get()));
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
        connections_.push_back(Connection{node.get(), nullptr, type_to_string<T>()});
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
