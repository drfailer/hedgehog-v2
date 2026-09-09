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

template <typename T>
struct Edge;

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
    // Since the edge is stored by copy inside node inputs/outputs, we use a
    // shared pointer for the profiler. Note that this involves sharing the
    // profiler between multiple threads which is not a problem performance
    // wise because threads should operate on different profiles independently.
    std::shared_ptr<Profiler> profiler = std::make_shared<Profiler>();
    void *sender = nullptr;
    void *receiver = nullptr;
    void *graph = nullptr;
    EdgeTransfer<T> fun;

    Edge(void *sender, void *receiver, void *graph, EdgeTransfer<T> fun)
        : sender(sender), receiver(receiver), graph(graph), fun(std::move(fun)) {}

    Edge(void *sender, void *receiver, EdgeTransfer<T> fun)
        : sender(sender), receiver(receiver), fun(std::move(fun)) {}

    Edge(void *graph, EdgeTransfer<T> fun)
        : graph(graph), fun(std::move(fun)) {}

    Edge(Edge<T> const &edge) = default;
    Edge<T> &operator=(Edge<T> const &edge) = default;
    Edge(Edge<T> &&edge) = default;
    Edge<T> &operator=(Edge<T> &&edge) = default;

    void transfer(data_t<T> data, RuntimeInfo const &info) {
        fun(this, data, info);
    }
};

struct DirectEdgeBuilder {
    template <typename T>
    Edge<T> make_edge(auto args) {
        using Receiver = std::remove_pointer_t<decltype(args.receiver)>;
        using Graph = std::remove_pointer_t<decltype(args.graph)>;

        return Edge<T>(args.sender, args.receiver, args.graph, [](Edge<T> *e, data_t<T> data, RuntimeInfo const &info) {
            auto receiver = static_cast<Receiver *>(e->receiver);
            auto graph = static_cast<Graph *>(e->graph);

            receiver->push_data(std::move(data), info);
            if constexpr (requires { graph->executor().on_transfer(receiver, info); }) {
                graph->executor().on_transfer(receiver, info);
            }
        });
    }
};

// Connections /////////////////////////////////////////////////////////////////

//
// Connections stored by the graph (used for building dot file, or other
// operations on the graph).
//
// We don't store the edge directly because edges need to be taken by copy in
// inputs/outputs.
//

struct Connection {
    Profiler *profiler; // profiler from the edge
    Node *sender;
    Node *receiver;

    ProfilerReport profile() {
        std::ostringstream oss;
        oss << "edge_" << (void *)sender << "_" << (void *)receiver;
        return profiler->create_report(oss.str(), ProfileReportKind::Edge);
    }
};

} // end namespace hh

#endif
