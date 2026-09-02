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

#ifndef HEDGEHOG_TOOL_CONFIG
#define HEDGEHOG_TOOL_CONFIG

#include <type_traits>
#include "../tool/type_list.hpp"
#include "../impl/task/lock_queue_input.hpp"
#include "../impl/task/direct_output.hpp"
#include "../impl/graph/thread_executor.hpp"
#include "../impl/graph/graph_input.hpp"
#include "../impl/graph/graph_output.hpp"
#include "../impl/graph/graph_sink.hpp"

namespace hh {

// Defaults ////////////////////////////////////////////////////////////////////

template <typename InputList>
using DefaultNodeInput = type_list_dispatch<InputList, LockQueueNodeInput>;

template <typename OutputList>
using DefaultNodeOutput = type_list_dispatch<OutputList, DirectNodeOutput>;

template <typename InputList>
using DefaultGraphInput = type_list_dispatch<InputList, GraphInput>;

template <typename OutputList>
using DefaultGraphOutput = type_list_dispatch<OutputList, GraphOutput>;

using DefaultGraphExecutor = ThreadExecutor;

// Fields deducers /////////////////////////////////////////////////////////////

// Node Input //////////////////////////

template <typename Impl>
concept HasNodeInput = requires { typename Impl::node_input; };

template <typename Impl, typename Default>
struct deduce_node_input_type {
    using type = Default;
};

template <HasNodeInput Impl, typename Default>
struct deduce_node_input_type<Impl, Default> {
    using type = typename Impl::node_input;
};

// Node Output /////////////////////////

template <typename Impl>
concept HasNodeOutput = requires { typename Impl::node_output; };

template <typename Impl, typename Default>
struct deduce_node_output_type {
    using type = Default;
};

template <HasNodeOutput Impl, typename Default>
struct deduce_node_output_type<Impl, Default> {
    using type = typename Impl::node_output;
};

// Graph Executor //////////////////////

template <typename Impl>
concept HasExecutor = requires { typename Impl::executor; };

template <typename Impl, typename Default>
struct deduce_executor_type {
    using type = Default;
};

template <HasNodeOutput Impl, typename Default>
struct deduce_executor_type<Impl, Default> {
    using type = typename Impl::executor;
};

// task config /////////////////////////////////////////////////////////////////

template <typename Impl>
struct make_task_config {
    using InputTypes  = Impl::inputs;
    using OutputTypes = Impl::outputs;
    using Input  = typename deduce_node_input_type<Impl, DefaultNodeInput<InputTypes>>::type;
    using Output = typename deduce_node_output_type<Impl, DefaultNodeOutput<OutputTypes>>::type;
    using Task = Impl;
};

// make_graph //////////////////////////////////////////////////////////////////

template <typename Impl, typename Inputs, typename Outputs>
auto make_graph(std::shared_ptr<Impl> executor, std::string const &name = "Graph") {
    struct Config {
        using InputTypes = Inputs;
        using OutputTypes = Outputs;
        using Sink = type_list_dispatch<Outputs, GraphSink>; // TODO: how to specify this?
        using Input =  typename deduce_node_input_type<Impl, DefaultGraphInput<InputTypes>>::type;
        using Output = typename deduce_node_output_type<Impl, DefaultGraphOutput<OutputTypes>>::type;
        using Executor = Impl;
    };
    auto graph = std::make_shared<Graph<Config>>(executor, NodeInfo{name, 0});
    graph->construct_input();
    graph->construct_output();
    return graph;
}

template <typename Inputs, typename Outputs>
auto make_graph(std::string const &name = "Graph") {
    return make_graph<DefaultGraphExecutor, Inputs, Outputs>(std::make_shared<DefaultGraphExecutor>(), name);
}

} // end namespace hh

#endif
