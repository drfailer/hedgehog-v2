# Task

A *task* is an execution core that consumes and processes input data, and
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
void execute(auto ctx, hh::data_t<T> data);
void execute(hh::data_t<T> data); // the context is optional

// those functions may also be static
static void execute(auto ctx, hh::data_t<T> data);
static void execute(hh::data_t<T> data);
```

The task `outputs` indicates the data types that your task produces. To send a
result to the next task (we'll see wiring later), we use the `push_result`
function from the context.


## Execution Context

The execution context is passed to the task execute functions and allows
interacting with the node.

```cpp
struct Task {
    using inputs  = hh::type_list<int, float>;
    using outputs = hh::type_list<Result>;

    void execute(auto ctx, hh::data_t<int> data) {
        // Pushing data (in and out)
        ctx->push_data(hh::make_data<float>(4));        // push data to the current task input
        ctx->push_result(hh::make_data<Result>(*data)); // produce a result

        // Task information
        auto _ = ctx->name();
        auto _ = ctx->graph_name();
        auto _ = ctx->graph_id();
        auto _ = ctx->number_thread();
        auto _ = ctx->thread_index();
        auto _ = ctx->numa_id();
        auto _ = ctx->device_id();
        auto _ = ctx->rank();
        auto _ = ctx->info();     // full RuntimeInfo struct

        // Direct access to the graph node
        auto node = ctx->node();
    }
};
```

### `push_data` vs `push_result`

- `push_data<T>(data)`: sends data to the **current task's own input queue**.
  This is useful for recursive processing (e.g. iterative algorithms where a
  task feeds itself until convergence).
- `push_result<T>(data)`: sends data to the **output edges**, forwarding it to
  all connected downstream nodes.

### Context type

- The full type: `hh::NodeExecutionContext<hh::TaskNode<hh::make_task_config<Task>>>`.
  - `NodeExecutionContext<NodeType>` is parametrized with the type of the graph node.
  - For a task, the node type is `TaskNode<Config>`.
  - The configuration of a task is deduced from your `Task` type using the
    `make_task_config` function.


## Initialize and Finalize

Tasks can optionally implement `initialize` and `finalize` methods. These are
called per-thread: each thread initializes its own copy before consuming data,
and finalizes it before the thread is joined.

Both methods accept an optional execution context parameter:

```cpp
struct Task {
    using inputs  = hh::type_list<int>;
    using outputs = hh::type_list<int>;

    void initialize(auto ctx) {
        // ctx provides the same API as in execute (name, thread_index, etc.)
    }

    void initialize() {
        // or without context
    }

    void finalize(auto ctx) { /* ... */ }
    void finalize() { /* ... */ }

    void execute(auto ctx, hh::data_t<int> data) { /* ... */ }
};
```


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

    void execute(auto ctx, hh::data_t<int> data) { /* doing efficient allocations... */ }

    // Implement complex copy logic
    auto copy() {
        return std::make_shared<TaskWithAttributes>(thread_local_arena.capacity());
    }
};
```

**NOTE**: we don't use the copy constructor / operator so the Hedgehog copy
logic remains isolated from the C++ part (it makes Hedgehog copy logic more
explicit).


## Create a task node

```cpp
// Use the default constructor of `Task`
auto node = hh::make_task<Task>(number_threads /* default: 1 */, "Task Name" /* default: "Task" */);

// Manual construction for non default constructible types
auto node = hh::make_task(std::make_shared<Task>(args...), number_threads, "Task Name");
```


## Task configuration

It is also possible to override the task node input and output ports:

- **Input port**: manages the input data of the node. It allows to change the
  implementation of the queue and thread wake up strategy. The default input
  port type is `LockQueueNodeInput` which uses a mutex based queue and a
  condition variable.
- **Output port**: manages the output edges of the node. The `push_result`
  function sends data to every output edge. Customizing the output may allow to
  do some extra operation before transferring the data.

```cpp
struct Task {
    using inputs  = hh::type_list<int, float>;
    using outputs = hh::type_list<int, float>;

    // add the following to change the types of the ports
    using node_input  = CustomInputPort<int, float>;
    using node_output = CustomOutputPort<int, float>;

    // execute ...
};
```

Hedgehog provides the following input types:

- `LockQueueNodeInput`: condition variable and mutex based queue (default).
- `MoodycamelMPMCInput`: moodycamel MPMC lock-free queue with atomic trigger.
- `DakingMPSCInput`: daking MPSC lock-free queue with atomic trigger. MPSC
  means only one consumer thread, suitable for single-threaded tasks.
- `GroupNodeInput<GroupSize, InputType>`: wraps another input type and
  partitions threads into groups of `GroupSize`, each group getting its own
  input queue. Reduces contention when using many threads.

Output types:

- `EdgeSlots`: basic collection of output edges (default).
- `EdgeConnectors`: deferred connection (used internally by the graph).


## Lambda interface

The lambda interface allows to create tasks using lambdas (which is convenient
for creating small tasks on the fly when prototyping).

```cpp
// create a lambda task node
// Sep=2 means the first 2 types are inputs, the rest are outputs
// So: inputs = (int, float), outputs = (int, float)
auto node = hh::make_lambda_task<2, int, float, int, float>(2, "Lambda Task");

node->set_execute<int>([](auto ctx, auto data) {
    // process data...
});
node->set_execute<float>([captured_data](auto ctx, auto data) {
    // process data...
});
```

The first template argument `Sep` (separator) splits the type list: the first
`Sep` types become inputs, and the remaining types become outputs. This is the
same convention used by `make_graph`.
