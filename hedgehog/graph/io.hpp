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

#ifndef HEDGEHOG_GRAPH_IO_H
#define HEDGEHOG_GRAPH_IO_H

#include <type_traits>
#include <optional>
#include "../tool/helpers.hpp"

namespace hh {

/******************************************************************************/
/*                                   nodes                                    */
/******************************************************************************/

// Concepts ////////////////////////////////////////////////////////////////////

//
// Node input specifications.
//
// Those concepts are not used internally because they reduce compile times,
// but one can use them to verify custom implementations as needed.
//

struct SignalOpts {
    RuntimeInfo info;    // execution context
    size_t count;        // number of threads to signal
    size_t thread_index; // signal a particular thread
};

struct WaitResult {
    bool terminate; // used to leave the thread loop
    bool skip;      // used to skip execution in the thread loop (no data, or defered)
};

template <typename T, typename ...Inputs>
concept NodeInputTrait = std::default_initializable<T> && requires {
    //
    // The input can be initializable/finalizable. initialize_component does:
    // - calls `t.initialize(info)` if defined
    // - else calls `t.initialize()` if defined
    // - else does nothing.
    // finalize works the same way.
    //
    [](T t, InitializationInfo const &info) {
        initialize_component(&t, info);
        finalize_component(&t, info);
    };

    //
    // The input must allow threads to wait or be signaled.
    //
    [](T t, RuntimeInfo const &info, SignalOpts opts) {
        WaitResult result = t.wait(info);
        t.signal(opts);
    };

    //
    // Input is also responsible to pop and execute data.
    //
    []<typename Executor>(T t, std::shared_ptr<Executor> exec) {
        t.execute(exec, RuntimeInfo{});
    };

    //
    // Data can be pushed to the input.
    //
    ([](T t, std::shared_ptr<Inputs> data, RuntimeInfo const &info) {
        t.push_data(data, info);
     }, ...);

    //
    // Edges can be connected to the input.
    //
    ([](T t, Edge<Inputs> edge) {
        t.connect_edge(edge);
        // auto &edges = t.template edges<Inputs>();
     }, ...);
};

//
// Node output specifications.
//
// Those concepts are not used internally because they reduce compile times,
// but one can use them to verify custom implementations as needed.
//

template <typename T, typename ...Outputs>
concept NodeOutputTrait = std::default_initializable<T> && requires {
    //
    // The input can be initializable/finalizable. initialize_component does:
    // - calls `t.initialize(info)` if defined
    // - else calls `t.initialize()` if defined
    // - else does nothing.
    // finalize works the same way.
    //
    [](T t, InitializationInfo const &info) {
        initialize_component(&t, info);
        finalize_component(&t, info);
    };

    //
    // Result data can be sent through the output
    //
    ([](T t, std::shared_ptr<Outputs> data) {
        t.push_result(data, RuntimeInfo{});
     }, ...);

    //
    // Edges can be connected to the input.
    //
    ([](T t, Edge<Outputs> edge) {
        t.connect_edge(edge);
        auto &edges = t.template edges<Outputs>();
     }, ...);
};

// Node I/O ////////////////////////////////////////////////////////////////////

//
// Helper for building node (input + output + default api).
//

// WARN: we avoid concepts here on purpose to reduce compile time (may change later).
template <typename Config>
class NodeIO {
  public:
    using Input = Config::Input;
    using Output = Config::Output;

  private:
    //
    // We use std::optional to defer the object construction (so this class remains default constructible).
    //
    std::optional<Input> input_;
    std::optional<Output> output_;

  public:
    Input &input() { return *input_; }
    Input const &input() const { return *input_; }

    Output &output() { return *output_; }
    Output const &output() const { return *output_; }

    template <typename ...Args>
    void construct_input(Args &&...args) {
        input_.emplace(std::forward<Args>(args)...);
    }

    template <typename ...Args>
    void construct_output(Args &&...args) {
        output_.emplace(std::forward<Args>(args)...);
    }

    void initialize(InitializationInfo const &info) {
        initialize_component(&(*input_), info);
        initialize_component(&(*output_), info);
    }

    void finalize(InitializationInfo const &info) {
        finalize_component(&(*input_), info);
        finalize_component(&(*output_), info);
    }

    template <typename T>
    void connect_input_edge(Edge<T> edge) {
        input_->connect_edge(std::move(edge));
    }

    template <typename T>
    void connect_output_edge(Edge<T> edge) {
        output_->connect_edge(std::move(edge));
    }

    template <typename T>
    void push_data(std::shared_ptr<T> data, RuntimeInfo const &info) {
        input_->push_data(data, info);
    }

    template <typename T>
    void push_result(std::shared_ptr<T> data, RuntimeInfo const &info) {
        output_->push_result(data, info);
    }

    WaitResult wait(RuntimeInfo const &info) {
        return input_->wait(info);
    }

    template <typename Executable>
    void execute(Executable exec, RuntimeInfo const &info) {
        input_->execute(exec, info);
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
