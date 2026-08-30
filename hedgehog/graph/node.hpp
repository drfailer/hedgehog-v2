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

#ifndef HEDGEHOG_GRAPH_NODE
#define HEDGEHOG_GRAPH_NODE

#include <type_traits>
#include <memory>
#include "../tool/type_list.hpp"
#include "info.hpp"
#include "edge.hpp"

namespace hh {

// Node ////////////////////////////////////////////////////////////////////////

//
// Node interface used in the graph: things that the graph needs the node to do.
//

class Node {
    NodeInfo info_;

  public:
    Node(NodeInfo info) : info_(info) {}

    NodeInfo const &info() { return info_; }

    virtual void initialize(GraphInfo const &info) = 0;
    virtual void execute(ExecutionInfo const &info) = 0;
    virtual void finalize(GraphInfo const &info) = 0;
    virtual std::shared_ptr<Node> copy() = 0;
};

// Node I/O ////////////////////////////////////////////////////////////////////

//
// Helper for building node (input + output + default api).
//

template <typename Input, typename Output>
struct NodeIO {
    Input input;
    Output output;

    void initialize(NodeInfo const &info) {
        input.initialize(info);
        output.initialize(info);
    }

    void finalize(NodeInfo const &info) {
        input.finalize(info);
        output.finalize(info);
    }

    template <typename T>
    void connect_input_edge(Edge<T> edge) {
        input.connect_edge(std::move(edge));
    }

    template <typename T>
    void connect_output_edge(Edge<T> edge) {
        output.connect_edge(std::move(edge));
    }

    template <typename T>
    void push_data(std::shared_ptr<T> data, ExecutionInfo const &info) {
        input.push_data(data, info);
    }

    template <typename T>
    void push_result(std::shared_ptr<T> data, ExecutionInfo const &info) {
        output.push_result(data, info);
    }
};

//
// Helper for dispatching ports.
//

template <template <typename> class PortType, typename ...Types>
struct NodePorts : PortType<Types>... {
    template <typename T>
    void connect_edge(Edge<T> data) {
        PortType<T>::connect_edge(data);
    }
};

//
// Node input specifications.
//
// Those concepts are not used internally because they reduce compile times,
// but one can use them to verify custom implementations as needed.
//

struct SignalOpts {
    ExecutionInfo info;  // execution context
    size_t count;        // number of threads to signal
    size_t thread_index; // signal a particular thread
};

struct WaitResult {
    bool terminate;
};

template <typename T, typename ...Inputs>
concept NodeInputTrait = std::default_initializable<T> && requires(T *t) {
    // initialization / deinitialization
    t->initialize(NodeInfo{});
    t->finalize(NodeInfo{});
    // triggering
    {t->wait(ExecutionInfo{})} -> std::same_as<WaitResult>;
    t->signal(SignalOpts{});
    // execution
    []<typename Core>(std::shared_ptr<Core> core) { t->execute_consumers(core, ExecutionInfo{}); };
    // data reception
    ([](std::shared_ptr<Inputs> data) { t->push_data(data, ExecutionInfo{}); }, ...);
    // edges: since edges are directional, they are optional for the inputs
    // ([](Edge<T> edge) { t->connect_edge(edge); }, ...);
};

//
// Node output specifications.
//
// Those concepts are not used internally because they reduce compile times,
// but one can use them to verify custom implementations as needed.
//

template <typename T, typename ...Outputs>
concept NodeOutputTrait = std::default_initializable<T> && requires(T *t) {
    // initialization / deinitialization
    t->initialize(NodeInfo{});
    t->finalize(NodeInfo{});
    // result transmission
    ([](std::shared_ptr<Outputs> data) { t->push_result(data, ExecutionInfo{}); }, ...);
    // edges
    ([](Edge<Outputs> edge) { t->connect_edge(edge); }, ...);
};

} // end namespace hh

#endif
