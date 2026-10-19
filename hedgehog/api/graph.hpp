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

#ifndef HEDGEHOG_API_GRAPH_H
#define HEDGEHOG_API_GRAPH_H

#include <memory>

#include "../graph/graph.hpp"
#include "../tool/config.hpp"

//
// Graphs are used to manage nodes and create edges between them. A graph has a
// runner that will execute the node upon startup and untill graph shutdown.
//

namespace hh {

// graph types /////////////////////////////////////////////////////////////////

template <size_t Sep, typename ...Types>
using thread_graph_t = Graph<make_thread_graph_config<Sep, Types...>>;

template <size_t Sep, typename ...Types>
using serial_graph_t = Graph<make_serial_graph_config<Sep, Types...>>;

// graph ///////////////////////////////////////////////////////////////////////

//
// Creates a graph using a given runner and io types with the default graph
// configuration (thread safe sink, direct edge builder).
//
template <typename Impl, size_t Sep, typename ...Types>
auto make_graph(std::shared_ptr<Impl> runner, std::string const &name = "Graph") {
    using Config = make_graph_config<Impl, Sep, Types...>;
    return std::make_shared<Graph<Config>>(runner, NodeInfo{name, 0});
}

//
// Creates a graph using the default graph runner (binds threads to nodes).
//
template <size_t Sep, typename ...Types>
auto make_graph(std::string const &name = "Graph") {
    return make_graph<DefaultGraphRunner, Sep, Types...>(std::make_shared<DefaultGraphRunner>(), name);
}

// serial graph ////////////////////////////////////////////////////////////////

//
// Creates a graph using the serial runner.
//
template <size_t Sep, typename ...Types>
auto make_serial_graph(std::string const &name = "Graph") {
    auto runner = std::make_shared<SerialRunner>();
    using Config = make_serial_graph_config<Sep, Types...>;
    return std::make_shared<Graph<Config>>(runner, NodeInfo{name, 0});
}

} // end namespace hh

#endif
