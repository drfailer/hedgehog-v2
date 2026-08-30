#ifndef HEDGEHOG_EXECUTION_TASK
#define HEDGEHOG_EXECUTION_TASK

#include "../graph/task_node.hpp"
#include "../graph/nodeio/lock_queue_input.hpp"
#include "../graph/nodeio/direct_output.hpp"
#include "../tool/config.hpp"

namespace hh {

template <typename Impl>
class Task {
    using NodeType = TaskNode<Impl>;
    NodeType *node_;
    ExecutionInfo info_;

  public:
    using config = make_task_config<Impl>;

    void task_initialize(NodeType *node, ExecutionInfo const &info) {
        auto impl = static_cast<Impl *>(this);
        if constexpr (requires { impl->initialize(info); }) {
            impl->initialize(info);
        } else if constexpr (requires { impl->initialize(); }) {
            impl->initialize();
        }
        this->node_ = node;
        this->info_ = info;
    }

    void task_finalize(NodeType *node, ExecutionInfo const &info) {
        auto impl = static_cast<Impl *>(this);
        if constexpr (requires { impl->finalize(info); }) {
            impl->finalize(info);
        } else if constexpr (requires { impl->finalize(); }) {
            impl->finalize();
        }
    }

    NodeType *node() {
        return this->node_;
    }

    ExecutionInfo info() {
        return this->info_;
    }

    template <typename T>
    void push_result(std::shared_ptr<T> data) {
        node->push_result(data, info);
    }
};

}

#endif
