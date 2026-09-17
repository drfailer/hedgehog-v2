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

#ifndef HEDGEHOG_API_EXECUTION_CONTEXT_H
#define HEDGEHOG_API_EXECUTION_CONTEXT_H

namespace hh {

// rumtime context /////////////////////////////////////////////////////////////

//
// used as an interface that querries the runtime info.
//

struct RuntimeContext {
    RuntimeInfo info_;

    RuntimeInfo const &info() const { return info_; }
    Profiler &profiler() { return *info_.profiler; }
    std::string const &name() const { return info_.node->name; }
    std::string const &graph_name() const { return info_.graph->name; }
    int graph_id() const { return info_.graph->id; }
    size_t number_thread() const { return info_.node->number_threads; }
    size_t thread_index() const { return info_.exec.thread_index; }
    int numa_id() const { return info_.exec.pipeline.numa_id; }
    int device_id() const { return info_.exec.pipeline.device_id; }
    int rank() const { return info_.exec.rank; }
};

// node execution context //////////////////////////////////////////////////////

template <typename NodeType>
struct NodeExecutionContext : RuntimeContext {
    NodeType *node_;

    // constructor used by the node
    void construct(NodeType *node, RuntimeInfo const &info) {
        node_ = node;
        RuntimeContext::info_ = info;
    }

    NodeType &node() { return *node_; }

    template <typename T>
    void push_data(data_t<T> data) {
        node_->input().push_data(std::move(data), RuntimeContext::info());
    }

    template <typename T>
    void push_result(data_t<T> data) {
        node_->output().push_data(std::move(data), RuntimeContext::info());
    }
};

// lambda execution context ////////////////////////////////////////////////////

//
// The node execution context gives access directly to the node, but we cannot
// do that for the lambda task (recursive type dependencies). This is a less
// powerful version that allows to build the lambda task (less powerful in the
// sens that it doesn't give full access to the node).
//

template <typename T>
struct LambdaExecutionContext : RuntimeContext {
    using PushDataFun = void (*)(void *, data_t<T>);
    using PushResultFun = void (*)(void *, data_t<T>);

    void *ctx_;
    PushDataFun push_data_;
    PushResultFun push_result_;

    template <typename ExecutionContext>
    LambdaExecutionContext(ExecutionContext *ctx) {
        ctx_ = ctx;
        RuntimeContext::info_ = ctx->info();
        push_data_ = [](void *ctx, data_t<T> data) { reinterpret_cast<ExecutionContext *>(ctx)->push_data(std::move(data)); };
        push_result_ = [](void *ctx, data_t<T> data) { reinterpret_cast<ExecutionContext *>(ctx)->push_result(std::move(data)); };
    }

    void push_data(data_t<T> data) { push_data_(ctx_, std::move(data)); }
    void push_result(data_t<T> data) { push_result_(ctx_, std::move(data)); }
};

}

#endif
