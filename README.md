# Hedgehog v2

## Objective

- Remove some API boilerplate to simplify task implementation:
    - `AbstractTask` constructor.
    - Defining task inputs/outputs (repetition in constructor/copy function).
- Simplify the connection system:
    - Sender/Receiver/Notifier/Slot too complicated.
- Simplify node configuration and remove core interfaces:
    - Too many abstraction for both the core and API.
- Fix useless allocations (see `copyInnerStructure`).
- Remove `canTerminate`:
    - System complicated, potentially slow and very error prone.
    - Doesn't work with sub-graphs.
- Try to reduce compile time.
- Give more context to node components to allow more custom implementations
  (ex: `ExecutionInfo` holding a thread id useful for custom receivers).

## Core ideas

- A task is not a node: currently, Hedgehog treats tasks as nodes which results
  in a lot of useless allocations and no distinction between the thread shared
  and local data. In the new version, a task is just an execution policy (with
  data; we can still copy the task). The task will be duplicated per threads (if
  necessary), but the node will not because node inputs/outputs (and other
  components if needed) are shared between threads.
- Node configuration: most of the complicated interfaces from the core have
  been removed. Now, a node just has a task (user implementation / lambdas
  task), an input and an output (other modules like the profiler will be added
  later). The types of the node components are deduced through the `TaskNode`
  template argument. Here, we use a single `Config` type which can be augmented
  at will as it is easier to augment, pass around, faster for the compiler, and
  more flexible for custom implementations (no internal "machinery" is
  enforced). Components implementations need to provide a certain number of
  functions. Here, we use concepts instead of interfaces (we pass types around
  anyways, and it also makes things easier for the compiler). **Concepts are
  not tested automatically** and it is up to users to static assert their custom
  implementations (we don't run a concept for nothing to reduce compile time).
- The graph is similar to the node but the task is replaced by an executor: a
  task execute on data, an executor execute on nodes, and a pipeline executor
  (not here yet) executes on graphs.
- Connection simplification: Hedgehog connection system is complicated because
  of the `canTerminate` feature and how slots and notifiers are defined. Here,
  we don't enforce any API for the input/output of a node (we can use any
  type/implementation). Instead, we use type erasure (`std::function`) to glue
  nodes together (glue any node type to any other node type). Creating an edge
  between two nodes simply means defining a transfer function to move data from
  one node to another, and connect the edge to the nodes. To send data, we just
  call `edge(data)`. The transfer function implementation can be customized to
  allow edge queues, filtering, MPI communication, ...
- Use separated type lists for inputs/outputs instead of splitters.
- Use more general purpose types with reusable parts (like a queue) instead of
  small components (ex: task input type handles data reception + notification;
  an implementation may use general purpose queues / notification system).

## New API

Task definition:

```cpp
struct MyTaks : hh::Task<MyTask> {
    // inputs and outputs are defined once, and we don't repeat them
    using inputs = hh::type_list<int, float>;
    using outputs = hh::type_list<double>;

    // node input/output (receiver/sender implementations) are optional (hh
    // will pick default types if unspecified).
    using node_input = MyCustomReceiver<inputs>;
    static_assert(hh::NodeInputTrait<MyCustomReceiver<inputs>, int, float>, "The receiver must be valid =D");

    // using node_output = ???;

    // no override, everything is static

    void execute(std::shared_ptr<int> data) { add_result(std::make_shared<double>((double)*data)); }
    void execute(std::shared_ptr<float> data) { add_result(std::make_shared<double>((double)*data)); }
    std::shared_ptr<MyTask> copy() { return std::make_shared<MyTask>(); }
};
```

Using `hh::Task<MyTask>` is not an obligation if the `MyTask` provides the
proper fields/functions to the task node.

The library now uses functions to create the tasks:

```cpp
// the function creates nodes!
auto my_task_node = hh::make_task<MyTask>(nb_threads, "MyTask");
auto my_other_task_node = hh::make_task<MyOtherTask>(std::make_shared<MyOtherTask>(args), nb_threads, "MyTask");
```

Graph are implemented in a similar way. The library provides a default graph
implementation (equivalent to original `hh::Graph<...>`).

```cpp
auto graph = hh::make_graph<hh::type_list<int, float>, hh::type_list<double>>("MyGraph");
graph->edge<int>(my_task_node, my_other_task_node);
graph->edge<float>(my_task_node, my_other_task_node, [&](std::shared_ptr<float> data, ExecutionInfo const &info) {
    // fancy edge implementation!!!
    my_other_task_node->push_data(data, info);
});
// edges will also be implemented

// Graph node implements the Node interface and is reusable in a bigger graph.
// The user doesn't interact directly with the node interface (too tidious) and
// uses API helper functions implemented by the graph node (this is not implemented
// yet)

graph->start(); // initialize the nodes (including sub-graphs), and run the executor (spawn threads)
auto data_variant = graph->get_reuslt(); // get (blocking depending on the implementation) result
graph->stop(); // finalize the nodes and join the threads (you have to use `get_result` to block)
```

The `get_result` function is used to wait for the graph execution. When the
graph only produces a single result that the user don't want to keep, we could
add a `wait` function. This is better than the node disconnection system in my
opinion.

## Request for improvement ideas

- I'm not sure how to handle the API part yet (e.g. `hh::Task<Impl>`), because
  there is a bit of duplication between the node and its API which may be
  problematic later.
- The concepts for the input/output/nodes need to be refined.
- The info structs may be augmented/refined.

## Removing the node API

The node API is just extra functions that give access to the node and execution
information. Instead, we could pass the node and the execution info to the
execution functions, or force the user to implement `set_ctx(node, info)`.

```cpp
struct MyTask {
    using inputs = hh::type_list<int, float>;
    using outputs = hh::type_list<int, float>;

    void execute(auto ctx, std::shared_ptr<int> data) {
        auto name = ctx.name();
        auto tc   = ctx.number_threads();
        auto ti   = ctx.thread_index();
        ctx.push_result(data);
    }

    // would also allow writing different version of an execute

    void execute(hh::TaskNodeContext<MyTask> ctx, std::shared_ptr<double> data) {
        // ...
    }

    void execute(hh::CustomTaskNodeContext<MyTask> ctx, std::shared_ptr<double> data) {
        // ...
    }
};
```
