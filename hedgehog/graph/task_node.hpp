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

#ifndef HEDGEHOG_GRAPH_TASK_NODE
#define HEDGEHOG_GRAPH_TASK_NODE

#include <string>
#include <memory>

#include "node.hpp"
#include "../tool/helpers.hpp"

namespace hh {

//
// Configurable task node implementation.
//

template <typename Config>
struct TaskNode : Node, NodeIO<Config> {
    using InputTypes  = Config::InputTypes;
    using OutputTypes = Config::OutputTypes;
    using Task        = Config::Task;
    using IO          = NodeIO<Config>;
    // TODO: using Profiler = Config::Profiler;

    GraphInfo graph_info;
    std::vector<std::shared_ptr<Task>> tasks; // QUESTION: do we want pointers for the task?

    TaskNode(std::shared_ptr<Task> task, NodeInfo const &info): Node(info), tasks(info.number_threads) {
        create_component_copies(tasks.begin(), tasks.end(), task);
    }

    void initialize(GraphInfo const &info) override {
        graph_info = info;
        auto init_info = InitializationInfo{Node::info(), graph_info};
        IO::initialize(init_info);
        for (auto &task : tasks) {
            task->set_node(this);
            initialize_component(task, init_info);
        }
    }

    void execute(ExecutionInfo const &info) override {
        auto runtime_info = RuntimeInfo{Node::info(), graph_info, info};
        auto thread_task = tasks[info.thread_index];

        // TODO: tasks should be initialized here (a task/executor should only be called during the execution phase)

        thread_task->set_runtime_info(runtime_info);
        for (;;) {
            auto wait_result = IO::wait(runtime_info);
            if (wait_result.terminate) break;
            if (wait_result.skip) continue;
            IO::execute_consumers(thread_task, runtime_info);
        }
    }

    void finalize(GraphInfo const &info) override {
        auto init_info = InitializationInfo{Node::info(), graph_info};
        IO::initialize(init_info);
        for (auto &task : tasks) {
            finalize_component(task, init_info);
        }
    }

    std::shared_ptr<Node> copy() override {
        return std::make_shared<TaskNode<Config>>(copy_component(tasks[0]), Node::info());
    }
};

} // end namespace hh


#endif
