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

#ifndef HEDGEHOG_GRAPH_NODE_IO
#define HEDGEHOG_GRAPH_NODE_IO

#include <type_traits>
#include "../tool/helpers.hpp"

namespace hh {

// Concepts ////////////////////////////////////////////////////////////////////

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
    bool terminate; // used to leave the thread loop
    bool skip;      // used to skip execution in the thread loop (no data, or defered)
};

// TODO: this is badly defined!

template <typename T, typename ...Inputs>
concept NodeInputTrait = std::default_initializable<T> && requires(T *t) {
    // initialization / deinitialization
    // t->initialize(InitializationInfo{});
    // t->finalize(InitializationInfo{});
    // triggering
    {t->wait(RuntimeInfo{})} -> std::same_as<WaitResult>;
    t->signal(SignalOpts{});
    // execution
    []<typename Executor>(T *t, std::shared_ptr<Executor> exec) { t->execute_consumers(exec, RuntimeInfo{}); };
    // data reception
    ([](T *t, std::shared_ptr<Inputs> data) { t->push_data(data, RuntimeInfo{}); }, ...);
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
    // t->initialize(InitializationInfo{});
    // t->finalize(InitializationInfo{});
    // result transmission
    ([](T *t, std::shared_ptr<Outputs> data) { t->push_result(data, RuntimeInfo{}); }, ...);
    // edges
    ([](T *t, Edge<Outputs> edge) { t->connect_edge(edge); }, ...);
};

// Node I/O ////////////////////////////////////////////////////////////////////

//
// Helper for building node (input + output + default api).
//


// QUESTION: TaskIO vs GraphIO?

// WARN: we avoid concepts here on purpose to reduce compile time (may change later).
template <typename Config>
struct NodeIO {
    // QUESTION: composition vs inheritance
    // QUESTION: raw types vs pointers (dereferencing cost? allow user side construction?)
    using Input = typename Config::Input;
    using Output = typename Config::Output;
    Input input;
    Output output;

    void initialize(InitializationInfo const &info) {
        initialize_component(&input, info);
        initialize_component(&output, info);
    }

    void finalize(InitializationInfo const &info) {
        finalize_component(&input, info);
        finalize_component(&output, info);
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
    void push_data(std::shared_ptr<T> data, RuntimeInfo const &info) {
        input.push_data(data, info);
    }

    template <typename T>
    void push_result(std::shared_ptr<T> data, RuntimeInfo const &info) {
        output.push_result(data, info);
    }

    WaitResult wait(RuntimeInfo const &info) {
        return input.wait(info);
    }

    template <typename Executor>
    void execute_consumers(std::shared_ptr<Executor> exec, RuntimeInfo const &info) {
        input.execute_consumers(exec, info);
    }
};

// Node ports //////////////////////////////////////////////////////////////////

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

} // end namespace hh

#endif
