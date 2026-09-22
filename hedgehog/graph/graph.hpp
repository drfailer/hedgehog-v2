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
#include "edge.hpp"
#include "../tool/helpers.hpp"
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
    using EdgeBuilder = Config::EdgeBuilder;
    using Input       = type_list_dispatch<InputTypes, EdgeSlots>;
    using Output      = type_list_dispatch<OutputTypes, EdgeConnectors>;

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

    GraphInfo graph_info_ = {};
    Input input_   = {};
    Output output_ = {};
    std::shared_ptr<Executor> executor_ = {};
    Sink sink_ = {};
    std::set<std::shared_ptr<Node>> nodes_ = {};
    std::set<std::shared_ptr<Node>> input_nodes_ = {};
    std::set<std::shared_ptr<Node>> output_nodes_ = {};
    std::vector<Connection> connections_ = {};

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
    Executor *executor() const { return executor_.get(); }
    Sink const &sink() const { return sink_; }
    std::set<std::shared_ptr<Node>> const &nodes() const { return nodes_; }
    std::set<std::shared_ptr<Node>> const &input_nodes() const { return input_nodes_; }
    std::set<std::shared_ptr<Node>> const &output_nodes() const { return output_nodes_; }
    std::vector<Connection> const &connections() const { return connections_; }

    // user functions //////////////////////////////////////////////////////////

    void connect_sink() {
        type_list_map<OutputTypes>([this]<typename T>() {
            output_.connect(Edge<T>(&sink_, this, [](Edge<T> *e, data_t<T> data, RuntimeInfo const &info) {
                static_cast<Sink *>(e->receiver)->push_data(std::move(data), info);
            }));
        });
    }

    void create_input_connections() {
        type_list_map<InputTypes>([&]<typename T>() {
            auto edges = input_.template edges<T>();
            for (auto &edge : edges) {
                connections_.push_back(Connection{
                    .sender = this,
                    .receiver = reinterpret_cast<Node *>(edge.receiver),
                    .type_name = type_to_string<T>(),
                });
            }
        });
    }

    void start(int rank = 0, HH_LOC) {
        if (this->parent() != nullptr) {
            log::fatal(loc, "a sub-graph cannot be started");
        }

        if (log::error_count() > 0) {
            log::fatal(loc, "graph start aborted due to errors.");
        }

        create_input_connections();

        // intialize the sink
        sink_.initialize(InitializationInfo{&Node::info(), &graph_info_, nullptr});
        connect_sink();


        Node::profiler().initialize();

        HH_PROFILE_REGION(Node::profiler(), "intialization") {
            initialize_components();
        }

        #ifdef HH_ENABLE_PROFILING
        exec_profile_ = Node::profiler().profile("execution");
        exec_profile_->begin_region();
        #endif
        ExecutionInfo exec_info = {0, rank, {0, 0}, false, ExecutionInfo::Execute};
        execute(exec_info);
    }

    void stop() {
        HH_PROFILE_REGION(Node::profiler(), "finalization") {
            finalize(GraphInfo{Node::info().name, 0});
        }
        #ifdef HH_ENABLE_PROFILING
        exec_profile_->end_region();
        #endif
    }

    template <typename T>
    void connect_output_edge(Edge<T> edge) {
        output_.connect(std::move(edge));
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        input_.push_data(std::move(data), info);
    }

    template <typename T>
    void push_data(data_t<T> data) {
        push_data<T>(std::move(data), RuntimeInfo{&Node::info(), &graph_info_, {}, &Node::profiler()});
    }

    auto get_result() {
        if constexpr (HasOnResult<Executor>) {
            executor_->on_result();
        }
        return sink_.get_result();
    }

    void eat_results(size_t count = 1) {
        for (size_t i = 0; i < count; ++i) {
            auto _ = get_result();
        }
    }

    // node api ////////////////////////////////////////////////////////////////

    void initialize_components() {
        auto init_info = InitializationInfo{&Node::info(), &graph_info_, &Node::profiler()};
        input_.initialize(init_info);
        output_.initialize(init_info);
        for (auto &node : nodes_) {
            node->initialize(graph_info_);
        }
        executor_->initialize(init_info);
    }

    void initialize(GraphInfo const &) override {
        Node::profiler().initialize();
        initialize_components();
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
        report.sender_id = reinterpret_cast<uintptr_t>(static_cast<Node *>(this));
        report.receiver_id = reinterpret_cast<uintptr_t>(&sink_);
        for (auto &node : nodes_) {
            report.add_report(node->profile());
        }
        uintptr_t edge_id = 0;
        for (auto const &conn : connections_) {
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
    // edge transfer functions. Output-side flattening uses deferred
    // EdgeConnectors that chain through sub-graph boundaries.
    //

    template <typename T>
    Edge<T> build_edge(auto sender, auto receiver) {
        return EdgeBuilder::template make_edge<T>(MakeEdgeArgs{sender, receiver, this});
    }

    void register_node(std::shared_ptr<Node> node) {
        if (node->parent() != nullptr) return;
        node->parent(this);
        nodes_.insert(node);
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
        register_node(sender);
        register_node(receiver);
        connections_.push_back(Connection{sender.get(), receiver.get(), type_to_string<T>()});
        sender->connect_output_edge(std::move(edge));
    }

    template <typename T>
    void draw_edge(auto sender, auto receiver, auto impl) {
        draw_edge<T>(sender, receiver, make_edge<T>(this, receiver.get(), std::move(impl)));
    }

    template <typename T>
    void draw_edge(auto sender, auto receiver) {
        if constexpr (IsGraph<typename decltype(receiver)::element_type>) {
            register_node(sender);
            register_node(receiver);
            auto& input_edges = receiver->input().template edges<T>();
            for (auto& input_edge : input_edges) {
                sender->connect_output_edge(input_edge);
                connections_.push_back(Connection{
                    .sender = sender.get(),
                    .receiver = reinterpret_cast<Node *>(input_edge.receiver),
                    .type_name = type_to_string<T>(),
                });
            }
        } else {
            draw_edge(sender, receiver, build_edge<T>(sender.get(), receiver.get()));
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
    void draw_edges(std::shared_ptr<Sender> sender, std::shared_ptr<Receiver> receiver, auto make_edge_fun, HH_LOC) {
        using sender_outputs = Sender::OutputTypes;
        using receiver_inputs = Receiver::InputTypes;
        size_t new_edge_count = 0;

        type_list_map<sender_outputs>([&]<typename T>() {
            if constexpr (type_list_contains<receiver_inputs, T>) {
                draw_edge(sender, receiver, make_edge_fun.template operator()<T>(MakeEdgeArgs{sender.get(), receiver.get(), this}));
                ++new_edge_count;
            }
        });

        if (new_edge_count == 0) {
            log::warning(loc, "failed to draw edges between `", type_to_string<Sender>(),
                         "` and `", type_to_string<Receiver>(), "` (no common types: `",
                         type_to_string<sender_outputs>(), "` and `",
                         type_to_string<receiver_inputs>(), "`).");
        }
    }

    template <typename Sender, typename Receiver>
    void draw_edges(std::shared_ptr<Sender> sender, std::shared_ptr<Receiver> receiver, HH_LOC) {
        using sender_outputs = Sender::OutputTypes;
        using receiver_inputs = Receiver::InputTypes;
        size_t new_edge_count = 0;

        type_list_map<sender_outputs>([&]<typename T>() {
            if constexpr (type_list_contains<receiver_inputs, T>) {
                draw_edge<T>(sender, receiver);
                ++new_edge_count;
            }
        });

        if (new_edge_count == 0) {
            log::warning(loc, "failed to draw edges between `", type_to_string<Sender>(),
                         "` and `", type_to_string<Receiver>(), "` (no common types: `",
                         type_to_string<sender_outputs>(), "` and `",
                         type_to_string<receiver_inputs>(), "`).");
        }
    }

    // inputs & outputs ////////////////////////////////////////////////////////

    //
    // Set graph inputs.
    //
    // When the connected node is a task, the edge transfering data to this
    // task is added to the edge slot list. When it is a sub-graph and no edge
    // is specified, the input edges of the sub-graph are added to the input
    // edge list of the current graph to guaranty direct data transfer to the
    // sub-graph's input nodes.
    //

    template <typename T>
    void connect_input(auto node, Edge<T> edge) {
        register_node(node);
        input_nodes_.insert(node);
        input_.connect_edge(std::move(edge));
    }

    template <typename T>
    void connect_input(auto node, auto impl) {
        connect_input<T>(node, make_edge<T>(this, node.get(), std::move(impl)));
    }

    template <typename T>
    void connect_input(auto node) {
        register_node(node);
        input_nodes_.insert(node);
        if constexpr (IsGraph<typename decltype(node)::element_type>) {
            auto& input_edges = node->input().template edges<T>();
            for (auto& input_edge : input_edges) {
                input_.connect_edge(input_edge);
            }
        } else {
            input_.connect_edge(build_edge<T>((Node *)nullptr, node.get()));
        }
    }

    template <typename Node>
    void connect_inputs(std::shared_ptr<Node> node, auto make_edge_fun, HH_LOC) {
        using node_inputs = Node::InputTypes;
        size_t new_connection_count = 0;

        type_list_map<InputTypes>([&]<typename T>() {
            if constexpr (type_list_contains<node_inputs, T>) {
                connect_input(node, make_edge_fun.template operator()<T>(MakeEdgeArgs{(Node*)nullptr, node.get(), this}));
                ++new_connection_count;
            }
        });

        if (new_connection_count == 0) {
            log::warning(loc, "failed to connect node `", type_to_string<Node>(),
                         "` as input (no common types: `", type_to_string<node_inputs>(),
                         "` and `", type_to_string<InputTypes>(), "`).");
        }
    }

    template <typename Node>
    void connect_inputs(std::shared_ptr<Node> node, HH_LOC) {
        using node_inputs = Node::InputTypes;
        size_t new_connection_count = 0;

        type_list_map<InputTypes>([&]<typename T>() {
            if constexpr (type_list_contains<node_inputs, T>) {
                connect_input<T>(node);
                ++new_connection_count;
            }
        });

        if (new_connection_count == 0) {
            log::warning(loc, "failed to connect node `", type_to_string<Node>(),
                         "` as input (no common types: `", type_to_string<node_inputs>(),
                         "` and `", type_to_string<InputTypes>(), "`).");
        }
    }

    //
    // Set graph outputs.
    //
    // Graph output doesn't add edges, instead it registers a lambda that will
    // connect given edges to the output node (the edge connection is deferred
    // because the receiver is not known yet). This allows creating direct
    // connections between sub-graphs output nodes and nodes in the parent
    // graph.
    //

    template <typename T>
    void connect_output(auto node) {
        using NodeType = decltype(node)::element_type;
        register_node(node);
        output_nodes_.insert(node);
        output_.template add_connect<T>([node, this](Edge<T> edge) {
            if constexpr (!IsGraph<NodeType> && !IsPipeline<NodeType>) {
                connections_.push_back(Connection{
                    .sender = node.get(),
                    .receiver = static_cast<Node *>(edge.receiver),
                    .type_name = type_to_string<T>(),
                });
            }
            node->connect_output_edge(std::move(edge));
        });
    }

    template <typename Node>
    void connect_outputs(std::shared_ptr<Node> node, HH_LOC) {
        using node_outputs = Node::OutputTypes;
        size_t new_connection_count = 0;

        type_list_map<OutputTypes>([&]<typename T>() {
            if constexpr (type_list_contains<node_outputs, T>) {
                connect_output<T>(node);
                ++new_connection_count;
            }
        });

        if (new_connection_count == 0) {
            log::warning(loc, "failed to connect node `", type_to_string<Node>(),
                         "` as output (no common types: `", type_to_string<node_outputs>(),
                         "` and `", type_to_string<OutputTypes>(), "`).");
        }
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
