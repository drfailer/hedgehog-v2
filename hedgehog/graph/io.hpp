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
#include "../tool/helpers.hpp"
#include "../tool/data.hpp"

namespace hh {

template <typename T>
struct Edge;

/******************************************************************************/
/*                                   nodes                                    */
/******************************************************************************/

// Concepts ////////////////////////////////////////////////////////////////////

struct SignalOpts {
    RuntimeInfo info;    // execution context
    size_t count;        // number of threads to signal
    size_t thread_index; // signal a particular thread
};

struct WaitResult {
    bool terminate; // used to leave the thread loop
    bool skip;      // used to skip execution in the thread loop (no data, or defered)
};

//
// Node input/output specifications.
//
// Those concepts are not used internally to reduce compile times, but one can
// enable them with HH_ENABLE_CONCEPTS to verify custom implementations.
//

#ifdef HH_ENABLE_CONCEPTS

template <typename T, typename ...Inputs>
concept NodeInputTrait = std::default_initializable<T>
    && requires(T t, InitializationInfo const &info) {
        t.initialize(info);
        t.finalize(info);
    }
    && requires(T t, RuntimeInfo const &ri, SignalOpts opts) {
        { t.wait(ri) } -> std::same_as<WaitResult>;
        t.signal(opts);
    }
    && requires(T t, void *exec, RuntimeInfo const &ri) {
        t.execute(exec, ri);
    }
    && (requires(T t, data_t<Inputs> d, RuntimeInfo const &i) {
        t.push_data(std::move(d), i);
    } && ...)
    && (requires(T t, Edge<Inputs> e) {
        t.connect_edge(std::move(e));
    } && ...);

template <typename T, typename ...Outputs>
concept NodeOutputTrait = std::default_initializable<T>
    && requires(T t, InitializationInfo const &info) {
        t.initialize(info);
        t.finalize(info);
    }
    && (requires(T t, data_t<Outputs> data) {
        t.push_result(data, RuntimeInfo{});
    } && ...)
    && (requires(T t, Edge<Outputs> e) {
        t.connect_edge(std::move(e));
        t.template edges<Outputs>();
    } && ...);

#endif // HH_ENABLE_CONCEPTS

// Node ports //////////////////////////////////////////////////////////////////

//
// Helper for dispatching ports.
//

template <template <typename> class PortType, typename ...Types>
struct NodePorts : PortType<Types>... {
    template <typename T>
    void connect_edge(Edge<T> data) {
        PortType<T>::connect_edge(std::move(data));
    }
};

} // end namespace hh

#endif
