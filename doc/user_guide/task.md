# Task

A *task* is an execution core that consumes and process input data, and
produces output data.

```cpp
struct Task {
    using inputs  = hh::type_list<int>; // input types
    using outputs = hh::type_list<int>; // output types

    void execute(auto ctx, hh::data_t<int> data) {
        *data += 2;                        // process an int
        ctx->push_result(std::move(data)); // produce an int
    }
};
```

## Inputs Outputs

The inputs and outputs are types, and they are specified through the `inputs`
and `outputs` attributes using the `hh::type_list` utility.

The task `inputs` indicates which data types the task may consume. When
consuming an input type, the task will process it using an `execute` function
(which must be implemented). The `execute` function for a type can have
different forms:

```cpp
void execute(auto ctx, data_t<T> data);
void execute(data_t<T> data); // the context is optional

// those functions may also be static
static void execute(auto ctx, data_t<T> data);
static void execute(data_t<T> data);
```

The task `outputs` indicates the data types that your task produces. To send a
result to the next task (we'll see wiring later), we use the `push_result`
function from the context.

## Execution Context

The execution context is passed to the task execute functions and allow to
interact with the node.

```
struct Task {
    using inputs  = hh::type_list<int, float>;
    using outputs = hh::type_list<Result>;

    void execute(auto ctx, data_t<int> data) {
        // Pushing data (in and out)
        ctx->push_data(hh::make_data<float>(4));        // push data to the current task input
        ctx->push_result(hh::make_data<Result>(*data)); // produce a result

        // Task information
        auto _ = ctx->name();
        auto _ = ctx->graph_name();
        auto _ = ctx->graph_id();
        auto _ = ctx->number_threads();
        auto _ = ctx->thread_index();
        auto _ = ctx->numa_id();
        auto _ = ctx->device_id();
        auto _ = ctx->rank();

        // Direct access to the graph node
        auto node = ctx->node();
    }
};
```

What is the type of the context?

- The full type: `hh::NodeExecutionContext<hh::TaskNode<hh::make_task_config<Task>>>`.
  - `NodeExecutionContext<NodeType>` is parametrized with the type of the graph node.
  - For a task, the node type is `TaskNode<Config>`.
  - The configuration of a task is deduced from you `Task` type using the
    `make_task_config` function.

TODO we need to add references to the developer guide with explanations about the config.

## Task's thread local data

To prevent data races, the tasks are copied so that each thread allocated to
the execution of this task owns a local copy. The copy is done through the
following logic:

- Hedgehog will try to use the `copy` method which should return a new shared
  pointer to a copy of the task.
- If the task doesn't implement `copy`, the copy will be created using the task
  type default constructor.
- If the task is not default constructible, the program will panic at runtime
  (we use runtime to provide a better error message).

```cpp
struct TaskWithAttributes {
    using inputs  = hh::type_list<int>;
    using outputs = hh::type_list<SomeCustomType>;

    hh::Arena thread_local_arena; // teasing allocators ;)

    TaskWithAttributes(size_t arena_size): thread_local_arena(arena_size) {}

    void execute(auto ctx, data_t<int> data) { /* doing efficient allocations... */ }

    // Implement complex copy logic
    auto copy() {
        return std::make_shared<TaskWithAttributes>(thread_local_arena.capacity());
    }
};
```

**NOTE**: we don't use the copy constructor / operator so the Hedgehog copy
logic remains isolated from the C++ part (it makes Hedgehog copy logic more
explicit).


# TODO make task
# TODO task configuration
