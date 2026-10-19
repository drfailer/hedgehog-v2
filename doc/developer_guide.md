# Hedgehog Developer Guide

This guide documents the internal architecture of the Hedgehog library for
contributors.

## Directory structure

- `hedgehog/`
  - `api/`: user-facing API (`make_*` functions, execution contexts, lambda
    APIs).
  - `graph/`: core graph components (nodes, edges, info structs).
  - `impl/`: pluggable implementations of graph components.
    - `graph/`: graph runners (`ThreadRunner`, `SerialRunner`, `TBBRunner`),
      sinks (`GraphSink`, `SerialSink`), edge builder (`DirectEdgeBuilder`).
    - `io/`: node input queues (`LockQueueNodeInput`, lock-free inputs,
      `GroupNodeInput`) and triggers (`CondTrigger`, `AtomicTrigger`).
    - `task/`: task specializations (`CudaTask`).
  - `tool/`: utilities (configuration, type lists, data wrappers, profiling,
    graph analysis, NUMA, logging, memory allocators).
    - `memory/`: allocators (`Arena`, `DynamicArena`), memory pools (`Pool`,
      `AutomaticPool`), lock-free index allocator (`IndexAllocator`).

## Design principles

### Info structs

Nodes are composed of different components (runner, input/output ports, ...).
These components are swappable (the component type can be changed statically)
and some implementations require access to many contextual values. To
facilitate future changes, we use info structs that carry all the information
available at a given stage of the execution. Changing the struct does not
require updating function signatures across the codebase.

Node-level info (known at graph construction time):

```cpp
struct PipelineInfo { int numa_id = -1; int device_id = 0; };
struct GraphInfo    { std::string name; int id; };
struct NodeInfo     { std::string name; size_t number_threads; };
struct RunInfo      { int rank; PipelineInfo pipeline; };
```

Phase-level info (distributed at different execution stages):

```cpp
// Passed to initialize/finalize of node components (input, output, runner).
struct InitializationInfo {
    NodeInfo const *node;
    GraphInfo const *graph;
    Profiler *profiler;
};

// Passed to all runtime functions (after threads are spawned).
struct RuntimeInfo {
    NodeInfo const *node;
    GraphInfo const *graph;
    RunInfo run;
    size_t thread_index = 0;
    Profiler *profiler;
};
```

### Template configuration

Similarly to info structs, template parameters are bundled into a single config
type rather than a list of types. Adding a configuration element does not
require changing template argument lists everywhere. It is also easier to pass
around.

For task nodes, `make_task_config<Impl>` deduces the configuration from the
user task type:

```cpp
template <typename Impl>
struct make_task_config {
    using InputTypes  = ...; // from Impl::inputs or Impl::io::inputs
    using OutputTypes = ...; // from Impl::outputs or Impl::io::outputs
    using Input  = ...; // from Impl::node_input or default (LockQueueNodeInput)
    using Output = ...; // from Impl::node_output or default (EdgeSlots)
    using Task = Impl;
};
```

For graphs, the config is built inline inside `make_graph`:

```cpp
struct Config {
    using InputTypes  = ...;
    using OutputTypes = ...;
    using Sink        = GraphSink<Outputs...>; // or SerialSink
    using Runner      = ThreadRunner;          // or SerialRunner, TBBRunner
    using EdgeBuilder = DirectEdgeBuilder;
};
```

### Type lists

Type lists (`hedgehog/tool/type_list.hpp`) are used extensively to manage
input/output types. Key operations:

| Meta-function                 | Description                                                 |
|-------------------------------|-------------------------------------------------------------|
| `type_list<Ts...>`            | Basic type list.                                            |
| `type_list_size<L>`           | Number of types in the list.                                |
| `type_list_append<L, Ts...>`  | Append types to the end.                                    |
| `type_list_prepend<L, Ts...>` | Prepend types to the beginning.                             |
| `type_list_dispatch<L, Tmpl>` | Explode a list into a template.                             |
| `type_list_apply<L, Tmpl>`    | Apply a unary template to each element.                     |
| `type_list_contains<L, T>`    | Check if `T` is in the list.                                |
| `type_list_map<L>(fn)`        | Invoke a generic lambda per type.                           |
| `io_types<Sep, Types...>`     | Split a pack into `::inputs`/`::outputs` at position `Sep`. |
| `MapType<Value, Keys...>`     | Compile-time type-indexed map.                              |

`io_types` is the mechanism behind `make_graph<Sep, Types...>`: the first
`Sep` types become inputs, the rest become outputs.

## Nodes

Unlike Hedgehog v1, user tasks and state managers are not graph nodes
themselves. They are execution cores attached to a graph node. This separates
thread-local data (task copy, execution context, profiler) from shared data
(input queues, output edges).

### Node interface

`Node` is the abstract base class for all graph-resident entities:

- `initialize(GraphInfo)` / `finalize(GraphInfo)` — called once by the graph.
- `run(RunInfo)` — starts execution (spawns threads or sub-graphs).
- `graph_view(GraphView*)` — builds the visualization tree.
- `profile(ProfileMap&)` — collects profiling data.

`Runnable` is the interface for nodes that consume data:

- `initialize(RuntimeInfo)` / `finalize(RuntimeInfo)` — per-thread setup.
- `wait(RuntimeInfo) → WaitResult` — blocks until data arrives or termination.
- `execute_all(RuntimeInfo)` / `execute_one(RuntimeInfo)` — drains the input
  queue.

### TaskNode

`TaskNode<Config>` inherits both `Node` and `Runnable`. It wraps a user
execution core (the "task").

**Thread states.** Each thread owns a `ThreadState` containing:
- A copy of the user task (`shared_ptr<Task>`). Thread 0 holds the original;
  threads 1..N get copies via `copy_component` during graph initialization.
- A `NodeExecutionContext` exposing `push_data` / `push_result`.
- A per-thread `Profiler`.

**Execute dispatch.** `ThreadState::execute<T>` uses `if constexpr` to detect
which signatures the user task supports:
1. `Task::execute(ctx, data)` — static method with context.
2. `Task::execute(data)` — static method without context.
3. `task->execute(ctx, data)` — member with context.
4. `task->execute(data)` — member without context.

### Graph

`Graph<Config>` inherits `Node`. It is the central orchestrator:
- Owns a set of nodes, input nodes, output nodes.
- Owns a `Runner`, a `Sink`, and the edge topology (`connections_`).
- Manages the graph lifecycle: `start()` → `run()` → `stop()`.

Key methods:
- `connect_inputs(node)` / `connect_outputs(node)` — wire graph boundary.
- `draw_edge<T>(sender, receiver)` / `draw_edges(sender, receiver)` — internal
  edges.
- `push_data<T>(data)` — inject data into the running graph.
- `get_result()` — blocking read from the sink.
- `eat_results(n)` — consume and discard `n` results.

**Build-once.** The first call to `start()` triggers `build_once()`, which
connects the sink edges and records input connections. Subsequent executions of
the same graph skip this step.

### PipelineNode

`PipelineNode<Config>` inherits `Node`. It manages multiple `Graph` instances
across NUMA nodes or GPU devices:
- Calls `pipeline->make_graph(i)` for each slot during construction.
- Routes data via `pipeline->send_to(data)`, which returns the target graph
  index.
- Propagates `PipelineInfo` (numa\_id, device\_id) to each graph's `RunInfo`.

## Edges

Edges connect nodes using type erasure to avoid coupling node types.

### Edge struct

```cpp
template <typename T>
struct Edge {
    void *graph;              // owning graph (for runner notification)
    void *receiver;           // receiving node (type-erased)
    EdgeTransfer<T> fun;      // transfer function (std::function)
};
```

Edges are stored by value (not pointer) in `EdgeSlot` vectors. Benchmarks show
that dereferencing a pointer per transfer has a measurable cost, so keeping
all edge data on the same cache line matters. The struct is `static_assert`'d
to fit in 64 bytes.

### EdgeTransferProxy

`EdgeTransferProxy<Config>` creates the actual transfer function that:
1. Casts `void*` pointers back to their concrete types.
2. Forwards to the user-provided edge implementation.
3. Calls `graph->runner()->on_transfer(receiver, info)` on success.

The proxy detects the user lambda signature via `if constexpr`:
- `(graph, receiver, data, info) → bool` — full access.
- `(receiver, data, info) → bool`
- `(data, info) → bool`

Returning `false` suppresses the runner notification (useful for filtering).

### EdgeSlots and EdgeConnectors

**`EdgeSlots<Types...>`** — graph input and node output component. Inherits
`EdgeSlot<T>` for each type. `push_data<T>` broadcasts to all edges of that
type (last edge receives via move, others via copy).

**`EdgeConnectors<Types...>`** — deferred edge connection for graph outputs.
Stores lambdas that will connect edges once the receiver is known. This
enables flattened connections across sub-graph boundaries.

### Sub-graph edge flattening

When `draw_edge<T>` or `connect_input<T>` detects that the receiver is a
`Graph`, it bypasses the sub-graph boundary: instead of pushing data to the
sub-graph's input, it copies the sub-graph's inner input edges directly into
the sender's output. This works recursively for nested sub-graphs.

Output-side flattening uses `EdgeConnectors`: `connect_output<T>` registers
a deferred connect lambda that will forward edges through the sub-graph
boundary when the parent graph connects the sub-graph to downstream nodes.

## Runners

Runners control how nodes are executed. They are selected via the graph config.

### ThreadRunner (default)

Spawns one `std::thread` per task thread. Each thread runs a simple loop:

```
initialize → loop { wait → execute_all } → finalize
```

Threads block in the node input's `wait()` function. NUMA-aware: pins
threads to the NUMA node specified by `PipelineInfo::numa_id`.
`on_transfer` is a no-op — relies on the OS scheduler.

### SerialRunner

Runs the entire graph on the caller's thread using an event-driven approach:
- `on_transfer` enqueues nodes into a ready queue.
- `execute_ready` drains the ready queue in a loop.
- `on_result` triggers `execute_ready` when the user calls `get_result`.

No separate threads are spawned. Uses re-entrancy guard (`executing_` flag)
to prevent recursive `execute_ready` calls.

### TBBRunner

Runs all nodes in a single OneTBB `task_arena`. Implements load balancing:
tracks per-node pending count, active thread count, and ready flag. Allocates
workers proportionally based on each node's configured `number_threads`
relative to total demand. Distinguishes parallel nodes (multiple concurrent
workers, batch processing) from serial nodes (mutual exclusion via `busy`
flag). Gated by `HH_ENABLE_TBB`.

## Node inputs and triggers

Node inputs decouple data reception from the runner. All inputs must provide
`push_data`, `wait`, `execute_one`, `execute_all`, `signal`, `initialize`,
and `finalize`.

### LockQueueNodeInput (default)

Per-type `std::queue` protected by `std::mutex`. Uses `CondTrigger`
(condition variable) for thread wakeup. Simple and correct baseline.

### Lock-free inputs

- **`MoodycamelMPMCInput`** — uses moodycamel's lock-free MPMC queue.
- **`DakingMPSCInput`** — uses a lock-free MPSC queue.

Both use `AtomicTrigger` (C++20 `atomic::wait`) instead of condition
variables.

### GroupNodeInput

Shards threads into groups of `GroupSize`, each with its own input instance.
Reduces contention at high thread counts.

### Triggers

**`CondTrigger`** — condition variable + mutex. Straightforward; used for
comparison with Hedgehog v1.

**`AtomicTrigger`** — uses `std::atomic<int32_t>::wait/notify` (C++20).
Non-accumulating signaling: single push does CAS 0→1 (idempotent), rapid
pushes do not inflate a counter. `wait()` does CAS decrement and
`std::atomic::wait(0)`.

## Execution context

Tasks are not nodes; they need a bridge to access node functionality.

**`RuntimeContext`** — exposes read-only runtime info (name, graph name,
thread index, NUMA id, device id, profiler, ...).

**`NodeExecutionContext<NodeType>`** — extends `RuntimeContext`. Provides
`push_data<T>()` (send data to self/input) and `push_result<T>()` (send data
downstream/output).

**`LambdaExecutionContext<T>`** — type-erased context for lambda tasks. Avoids
recursive type dependencies between the lambda task and the node. Uses function
pointers (`void*` + cast) to forward `push_data` / `push_result` calls.

## Data wrapper (`data_t`)

`data_t<T>` is a compile-time selectable wrapper around the data flowing
through edges:

| Define            | `data_t<T>`     | Semantics                   |
|-------------------|-----------------|-----------------------------|
| (none, default)   | `shared_ptr<T>` | Reference-counted, safe     |
| `HH_POINTER_MODE` | `T*`            | Raw pointer, user-managed   |
| `HH_VALUE_MODE`   | value wrapper   | Zero-allocation, move-based |

`make_data<T>(args...)` constructs data in the active mode.

## Memory system

### Allocators

**`Arena`** — fixed-size monotonic bump allocator. NUMA-aware: can allocate
on a specific NUMA node via `mmap` + `mbind`. Supports external buffers.
Thread-local by design. Provides `allocate<T>`, `reset`, `contains`.

**`DynamicArena`** — growing multi-block bump allocator (inspired by Odin's
dynamic arena). Allocates new blocks when the current one is exhausted.
Oversized requests go to a separate out-of-band list. Supports `resize` of
the last allocation, `reset`/`free_all`. Also NUMA-aware.

Both arenas provide `hh::make_shared<T>(arena, ...)`,
`hh::make_unique<T>(arena, ...)`, and `hh::make_data<T>(arena, ...)`.

### Pools

**`Pool<T>`** — concurrent fixed-capacity memory pool. Uses `IndexAllocator`
(Treiber stack, lock-free, ABA-safe with tagged pointers) for slot management
and `std::counting_semaphore` for blocking waits. Supports NUMA allocation
(`mmap` + `mbind`) and CUDA device selection.

**`AutomaticPool<T>`** — wraps `Pool<T>`, returns `shared_ptr<T>` whose custom
deleter automatically returns memory to the pool.

**`MultiPool<Types...>`** / **`MultiAutomaticPool<Types...>`** — multi-type
wrappers.

## Profiling

Controlled by compile-time defines:
- `HH_ENABLE_PROFILING` — enables timing via `Measure` (Welford's online
  algorithm for mean/stddev/min/max).
- `HH_USE_NVTX` — enables NVIDIA NVTX markers for GPU profiling tools.

**`Profiler`** — thread-safe (mutex-guarded) container for named `Profile`
objects. Methods: `profile_region(name)`, `profile_marker(name)`,
`add_string(label, args...)`.

**`HH_PROFILE_REGION(profiler, name) { ... }`** — measures the duration of
the enclosed block. Uses a for-loop trick to call `begin_region()` /
`end_region()`.

**Built-in profiled regions.** The library automatically profiles graph
`initialization`, `execution`, `finalization`, and per-input-type
`execute<T>` and `wait` in each task's `ThreadState`.

**Output:**
- `report_to_text(view, profiles, ostream)` — plain text.
- `graph_view_to_dot(view, profiles, ostream)` — Graphviz DOT with
  color-coded execution hotspots (blue → red gradient).
- `graph->generate_dot_file("output.dot")` — convenience method.

## NUMA and CUDA support

### NUMA

- **Thread pinning:** `numa::pin_current_thread(numa_id)` reads
  `/sys/devices/system/node/nodeN/cpulist`, sets affinity via
  `sched_setaffinity`. FreeBSD and Windows implementations also provided.
  No-op when `numa_id < 0`.
- **Memory allocation:** `numa::alloc_on_node(size, numa_id)` uses
  `mmap` + `mbind(MPOL_BIND)`. `free_on_node` uses `munmap`.
  `query_node(ptr)` verifies placement via `move_pages` syscall.
- **Integration:** `PipelineInfo` carries `numa_id` and `device_id`.
  `ThreadRunner` pins threads based on `pipeline.numa_id`. `Pool<T>` and
  `Arena` accept `numa_id` for NUMA-local allocation.

### CUDA

Gated by `HH_USE_CUDA`:
- **`CudaTask`** base class: creates a CUDA stream per task, calls
  `cudaSetDevice(device_id)` during initialization. Provides virtual hooks
  `initialize_cuda()` / `finalize_cuda()`.
- **`Pool<T>`** calls `cudaSetDevice(device_id)` before constructing pool
  elements.
- **`PipelineInfo::device_id`** propagates through the runtime; accessible
  via `ctx->device_id()`.

## Graph analysis

`hedgehog/tool/graph_analysis.hpp` provides:
- `ga_node_input_edges(view, node, edges)` / `ga_node_output_edges(...)` —
  query edge connectivity from a `GraphView`.
- `CycleDetector` — DFS-based cycle detection on the `GraphView`. Traverses
  all paths from graph inputs, prints each cycle found, and handles
  sub-graphs and pipelines recursively. Returns cycle count.
- `ga_cycle_detection(view)` — convenience wrapper.

## Graph lifecycle

A typical execution follows this sequence:

```
1. Construction    make_graph, make_task, connect_inputs/outputs, draw_edges
2. build_once()    Called by start(). Connects sink edges, records connections.
3. initialize()    Propagated to all nodes → copies tasks, initializes I/O.
4. run()           Runner spawns threads (ThreadRunner) or initializes event
                   loop (SerialRunner).
5. push_data()     User injects data into the graph.
6. get_result()    Blocks until the sink has data. SerialRunner processes
                   nodes synchronously on this call.
7. stop()          Triggers finalization. Runner joins threads.
```

The separation between `build_once()` and `start()` allows restarting a graph
without rebuilding the edge topology.
