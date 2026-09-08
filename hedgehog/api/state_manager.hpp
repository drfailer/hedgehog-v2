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

#ifndef HEDGEHOG_API_STATE_MANAGER_H
#define HEDGEHOG_API_STATE_MANAGER_H

#include <mutex>

#include "../graph/node.hpp"
#include "../graph/io.hpp"

namespace hh {

//
// The default state manager doesn't lock the state to allow users to define
// optimized state management.
//

template <typename State>
struct StateManager {
    using inputs = State::inputs;
    using outputs = State::outputs;

    template <typename T>
    struct OutputPort {
        std::vector<Edge<T>> edges_ = {};
        State *state_ = nullptr;

        void push_result(data_t<T> data, RuntimeInfo const &info) {
            // TODO: what kind of API do we want here?
            //       We could also use methods or members of the data to
            //       determin if we need to send
            //       NOTE: info will contain the graph id which will be used to
            //       determin the output
            if (!state_->should_transfer(data, info)) return;
            for (auto &edge : edges_) {
                edge.transfer(data, info);
            }
        }

        void connect_edge(Edge<T> edge) {
            edges_.push_back(std::move(edge));
        }
    };

    struct Output : type_list_dispatch<outputs, NodePorts, OutputPort> {
        // TODO: the make_state_manager should construct the output with the state
        Output(State *state) {
            type_list_map<outputs>([state]<typename T>() {
                OutputPort<T>::state_ = state
            });
        }

        template <typename T>
        void push_result(data_t<T> data, RuntimeInfo const &info) {
            DirectOutputPort<T>::push_result(std::move(data), info);
        }

        template <typename T>
        std::vector<Edge<T>> &edges() {
            return DirectOutputPort<T>::edges_;
        }
    };
    using node_output = Output;

    std::shared_ptr<State> state_;

    StateManager(std::shared_ptr<State> state) : state_(state) {}

    void execute(auto ctx, auto data) {
        state_->execute(ctx, data);
    }

    std::shared_ptr<Node> *copy() {
        // TODO: use log to print a proper error message and crash using exit
        throw "a state manager should not be copied";
    }
};

} // end namespace hh

#endif
