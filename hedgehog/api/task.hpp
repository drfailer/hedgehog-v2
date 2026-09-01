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

namespace hh {

template <typename Impl>
struct Task : ExecutionContext<TaskNode<make_task_config<Impl>>> {};

template <typename Impl>
auto make_task(std::shared_ptr<Impl> task, size_t number_threads = 1, std::string const &name = "Task") {
    using Config = make_task_config<Impl>;
    return std::make_shared<TaskNode<Config>>(task, NodeInfo{name, number_threads});
}

template <typename Impl>
auto make_task(size_t number_threads = 1, std::string const &name = "Task") {
    return make_task(std::make_shared<Impl>(), number_threads, name);
}

//
// Aternate syntax (execute takes an execution context and data as argument):
//
// Supportting both inheriting from Task, and not (taking the context as
// argument in execute) adds a lot of useless complexity.
//

//
// template <typename Impl>
// struct ContextTaskWrapper : Task<Impl> {
//     std::shared_ptr<Impl> impl;
//
//     ContextTaskWrapper(std::shared_ptr<Impl> impl) : impl(impl) {}
//
//     template <typename T>
//     void execute(std::shared_ptr<T> data) {
//         auto &ctx = *static_cast<Task<Impl> *>(this);
//         impl->execute(ctx, data);
//     }
//
//     std::shared_ptr<Impl> copy() {
//         return impl->copy();
//     }
// };
//
// template <typename Impl>
// struct TaskWrapper {
//     /* allow to use impl functions throug `operator.` instead of `operator->` */
// };
//
// /* we can then store this instead of std::shared_ptr<Task> in TaskNode */
// template <typename Impl>
// using ThreadTask = std::conditional_t<std::is_convertible_v<Impl, Task<Impl>>
//                                       TaskWrapper<Impl>,
//                                       ContextTaskWrapper<Impl>>;
//

}

#endif
