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

#ifndef HEDGEHOG_API_TASK
#define HEDGEHOG_API_TASK

#include "../graph/task_node.hpp"
#include "../tool/config.hpp"
#include "execution_context.hpp"

#include <tuple>

namespace hh {

// make_task ///////////////////////////////////////////////////////////////////

template <typename Impl>
auto make_task(std::shared_ptr<Impl> task, size_t number_threads = 1, std::string const &name = "Task") {
    using Config = make_task_config<Impl>;
    return std::make_shared<TaskNode<Config>>(task, NodeInfo{name, number_threads});
}

template <typename Impl>
auto make_task(size_t number_threads = 1, std::string const &name = "Task") {
    return make_task(std::make_shared<Impl>(), number_threads, name);
}

// lambda task /////////////////////////////////////////////////////////////////

template <typename T>
using LambdaExecute = std::function<void(LambdaExecutionContext<T> *, data_t<T>)>;

template <typename ...Inputs>
struct LambdaTask {
    using ExecuteList = std::tuple<LambdaExecute<Inputs>...>;

    ExecuteList executes_ = {};

    template <typename T>
    void execute(auto ctx, data_t<T> data) {
        LambdaExecutionContext<T> lctx(ctx);
        std::get<LambdaExecute<T>>(executes_)(&lctx, std::move(data));
    }

    template <typename T>
    void set_lambda(LambdaExecute<T> execute) {
        std::get<LambdaExecute<T>>(executes_) = std::move(execute);
    }

    auto copy() {
        auto cpy = std::make_shared<LambdaTask<Inputs...>>();
        cpy->executes_ = this->executes_;
        return cpy;
    }
};

template <typename Config>
auto make_lambda_task(size_t number_threads = 1, std::string const &name = "LambdaTask") {
    auto lambda_task = std::make_shared<typename Config::Task>();
    return std::make_shared<TaskNode<Config>>(lambda_task, NodeInfo{name, number_threads});
}

template <size_t Sep, typename ...Types>
auto make_lambda_task(size_t number_threads = 1, std::string const &name = "LambdaTask") {
    using io = io_types<Sep, Types...>;
    struct Config {
        using InputTypes  = io::inputs;
        using OutputTypes = io::outputs;
        using Input  = DefaultNodeInput<InputTypes>;
        using Output = DefaultNodeOutput<OutputTypes>;
        using Task = type_list_dispatch<typename io::inputs, LambdaTask>;
    };
    return make_lambda_task<Config>(number_threads, name);
}

} // end namespace hh

#endif
