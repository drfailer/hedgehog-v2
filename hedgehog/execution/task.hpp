#ifndef HEDGEHOG_EXECUTION_TASK
#define HEDGEHOG_EXECUTION_TASK

#include "../graph/task_node.hpp"
#include "../graph/nodeio/lock_queue_input.hpp"
#include "../graph/nodeio/direct_output.hpp"
#include "../tool/config.hpp"

namespace hh {

template <typename Impl>
class Task {
  public:
    using Config = make_task_config<Impl>;
    using NodeType = TaskNode<Config>;

  private:
    NodeType *node_;
    ExecutionInfo info_;

    friend NodeType;
    void set_node(NodeType *node) { node_ = node; }
    void set_execution_info(ExecutionInfo const &info) { info_ = info; }

  public:
    std::string const &name() { return node_->info().name; }
    size_t number_thread() { return node_->info().number_thread; }
    size_t thread_index() { return info_.thread_index; }

    template <typename T>
    void push_result(std::shared_ptr<T> data) {
        node_->push_result(data, info_);
    }
};

}

#endif
