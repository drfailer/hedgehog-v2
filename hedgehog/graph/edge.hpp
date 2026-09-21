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

#ifndef HEDGEHOG_GRAPH_EDGE
#define HEDGEHOG_GRAPH_EDGE

#include <functional>
#include <memory>
#include <cassert>
#include "../tool/data.hpp"
#include "../tool/concepts.hpp"
#include "../tool/profiling.hpp"
#include "info.hpp"
#include "node.hpp"

namespace hh {

//
// Edges are used to connect nodes. Here, we use type erasure to be able to
// connect any node type to any other node type. It also makes things more
// manageable because it avoids adding too many interfaces everywhere (reduces
// interdependencies).
//
// Nodes inputs/outputs store a raw edge struct instead of a pointer because
// benchmarks have shown that dereferencing the pointer had a non negligible
// impact on performance (this way, all the data required by an edge is loaded
// with the edge).
//

//
// For user customization purpose, we use an std::function which allow users to
// capture content when defining custom edges. If the captured content fits in
// the std::function small buffer, it should not impact the performance (cf
// SBO).
//
// Note that using a function pointer here would be more efficient but less
// practical API wise.
//
template <typename T>
using EdgeTransfer = std::function<void(Edge<T> *, data_t<T>, RuntimeInfo const &)>;

template <typename T>
struct Edge {
    void *receiver = nullptr;
    void *graph = nullptr;
    EdgeTransfer<T> fun;

    Edge(void *receiver, void *graph, EdgeTransfer<T> fun)
        : receiver(receiver), graph(graph), fun(std::move(fun)) {}

    Edge(void *graph, EdgeTransfer<T> fun)
        : graph(graph), fun(std::move(fun)) {}

    Edge(EdgeTransfer<T> fun)
        : fun(std::move(fun)) {}

    Edge(Edge<T> const &edge) = default;
    Edge<T> &operator=(Edge<T> const &edge) = default;
    Edge(Edge<T> &&edge) = default;
    Edge<T> &operator=(Edge<T> &&edge) = default;

    void transfer(data_t<T> data, RuntimeInfo const &info) {
        fun(this, std::move(data), info);
    }
};

// Edge Impl ///////////////////////////////////////////////////////////////////

//
// The Edge struct holds an std::function which handles the transfer as well
// as some required information (help with SBO). It is an opaque interface that
// can be stored inside the graph components. The EdgeImpl is the actual edge
// implementation that knows the type of all the elements.
//
// Using this is not required, but it makes creating custom edges simpler,
// allowing to pass a lambda with a more flexible interface instead of an edge.
// EdgeImpl also execute Executor::on_transfer automatically (required for some
// executor implementations). Users still have the possiblity to directly
// provide an Edge when full control over the implementaion is required.
//

template <typename Config>
struct EdgeImpl {
    using Type = Config::Type;
    using Impl = Config::Impl;
    using Graph = Config::Graph;
    using Receiver = Config::Receiver;

    Impl impl;

    EdgeImpl(Impl impl): impl(std::move(impl)) {}

    void operator()(Edge<Type> *edge, data_t<Type> data, RuntimeInfo const &info) {
        using Executor = Graph::Executor;

        auto receiver = static_cast<Config::Receiver *>(edge->receiver);
        auto graph = static_cast<Graph *>(edge->graph);

        if constexpr (requires { impl(graph, receiver, std::move(data), info); }) {
            impl(graph, receiver, std::move(data), info);
        } else if constexpr (requires { impl(receiver, std::move(data), info); }) {
            impl(receiver, std::move(data), info);
        } else {
            impl(std::move(data), info);
        }

        if constexpr (HasOnTransfer<Executor, Receiver, RuntimeInfo>) {
            graph->executor()->on_transfer(receiver, info);
        }
    }
};

template <typename T>
Edge<T> make_edge(auto graph, auto receiver, auto impl) {
    struct Config {
        using Type = T;
        using Impl = decltype(impl);
        using Graph = std::remove_pointer_t<decltype(graph)>;
        using Receiver = std::remove_pointer_t<decltype(receiver)>;
    };
    return Edge<T>(graph, receiver, EdgeImpl<Config>(impl));
}

// Connections /////////////////////////////////////////////////////////////////

//
// Connections stored by the graph (used for building dot file, or other
// operations on the graph).
//
// We don't store the edge directly because edges need to be taken by copy in
// inputs/outputs.
//

struct Connection {
    Node *sender;
    Node *receiver;
    std::string type_name;
};

// Edge slot ///////////////////////////////////////////////////////////////////

//
// An edge slot is just a collection of edges. This type can be used as graph
// input or task node output.
//

template <typename T>
struct EdgeSlot {
    std::vector<Edge<T>> edges_;
};

template <typename ...Types>
struct EdgeSlots : EdgeSlot<Types>... {
    void initialize(InitializationInfo const &) {}
    void finalize(InitializationInfo const &) {}

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        auto &edges = EdgeSlot<T>::edges_;
        for (size_t i = 0, n = edges.size(); i + 1 < n; ++i) {
            edges[i].transfer(data, info);
        }
        if (!edges.empty()) {
            edges.back().transfer(std::move(data), info);
        }
    }

    template <typename T>
    void connect_edge(Edge<T> edge) {
        EdgeSlot<T>::edges_.push_back(std::move(edge));
    }

    template <typename T>
    std::vector<Edge<T>> &edges() { return EdgeSlot<T>::edges_; }

    size_t edge_count() { return (EdgeSlot<Types>::edges_.size() + ...); }
};


// Edge connector //////////////////////////////////////////////////////////////

//
// The edge connector is used to defer edge connection. This is mainly used to
// connect graph output nodes as it allows making straight connections accross
// sub-graph boundaries.
//
// Note that we could use interfaces instead of this (we don't need optimal
// performance for the graph initialization machinery), but std::function
// allows to do this witout adding extra inheritance layer in every node
// implementation.
//

template <typename T>
using ConnectFunction = std::function<void(Edge<T>)>;

template <typename T>
struct EdgeConnector {
    std::vector<ConnectFunction<T>> connects_;
};

template <typename ...Types>
struct EdgeConnectors : EdgeConnector<Types>... {
    void initialize(InitializationInfo const &) {}
    void finalize(InitializationInfo const &) {}

    template <typename T>
    void connect(Edge<T> edge) {
        for (auto &connect : EdgeConnector<T>::connects_) {
            connect(edge);
        }
    }

    template <typename T>
    void add_connect(ConnectFunction<T> connect) {
        EdgeConnector<T>::connects_.push_back(std::move(connect));
    }
};

} // end namespace hh

#endif
