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

namespace hh {

//
// The default state manager doesn't lock the state to allow users to define
// optimized state management.
//

template <typename State>
struct StateManager {
    using inputs = State::inputs;
    using outputs = State::outputs;

    std::shared_ptr<State> state_;

    StateManager(std::shared_ptr<State> state) : state_(state) {}

    void execute(auto ctx, auto data) {
        state_->execute(ctx, std::move(data));
    }

    std::shared_ptr<StateManager> copy() {
        log::fatal("A state manager should not be copied.");
    }
};

template <typename Impl>
auto make_state_manager(std::shared_ptr<Impl> state, std::string const &name = "StateManager") {
    using BaseConfig = make_task_config<Impl>;
    struct Config : BaseConfig { using Task = StateManager<Impl>; };
    return std::make_shared<TaskNode<Config>>(std::make_shared<StateManager<Impl>>(std::move(state)),
                                              NodeInfo{name, 1});
}

template <typename State>
struct LockStateManager {
    using inputs = State::inputs;
    using outputs = State::outputs;

    std::mutex mutex_;
    std::shared_ptr<State> state_;

    LockStateManager(std::shared_ptr<State> state) : state_(state) {}

    void execute(auto ctx, auto data) {
        mutex_.lock();
        state_->execute(ctx, std::move(data));
        mutex_.unlock();
    }

    std::shared_ptr<LockStateManager> copy() {
        log::fatal("A state manager should not be copied.");
    }
};


template <typename Impl>
auto make_lock_state_manager(std::shared_ptr<Impl> state, std::string const &name = "StateManager") {
    using BaseConfig = make_task_config<Impl>;
    struct Config : BaseConfig { using Task = LockStateManager<Impl>; };
    return std::make_shared<TaskNode<Config>>(std::make_shared<LockStateManager<Impl>>(std::move(state)),
                                              NodeInfo{name, 1});
}

} // end namespace hh

#endif
