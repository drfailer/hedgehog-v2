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

#include <type_traits>
#include <memory>
#include <cassert>
#include "../tool/type_list.hpp"
#include "info.hpp"

namespace hh {

//
// Bridge between nodes. Here we use type erasure to allow connecting nodes
// with different sender/receiver types. One can also implement a custom edge
// for various purpose (MPI edge for instance).
//

template <typename T>
using Edge = std::function<void(std::shared_ptr<T>, RuntimeInfo const &)>;

struct DirectEdgeBuilder {
    template <typename T>
    Edge<T> make_edge(auto args) {
        return [args](std::shared_ptr<T> data, RuntimeInfo const &info) {
            assert(args.receiver != nullptr);
            assert(args.executor != nullptr);

            args.receiver->push_data(data, info);

            if constexpr (requires { args.executor->on_transfer(args.receiver, info); }) {
                args.executor->on_transfer(args.receiver, info);
            }
        };
    }
};

} // end namespace hh

#endif
