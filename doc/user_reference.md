# Hedgehog User Reference

Quick API reference. For tutorials and in-depth explanations, see the
[user guide](user_guide/README.md). This reference uses `optional` and
`required` as annotations — treat them as empty macros.

---

## Compile-time defines

| Define                 | Effect                                                      |
|------------------------|-------------------------------------------------------------|
| `HH_VALUE_MODE`        | `data_t<T>` = value wrapper (default: `std::shared_ptr<T>`) |
| `HH_POINTER_MODE`      | `data_t<T>` = `T*`                                          |
| `HH_USE_CUDA`          | Enables CudaTask                                            |
| `HH_ENABLE_CHECK_CUDA` | Enables `hh_check_cuda_error` runtime checks                |
| `HH_ENABLE_PROFILING`  | Enables profiling regions/markers/strings                   |
| `HH_USE_NVTX`          | Enables NVTX integration                                    |

---

## Data ([guide](user_guide/data_t.md))

```cpp
template <typename T> data_t<T> make_data(auto &&...args);
template <typename T> data_t<T> make_data(Arena &arena, auto &&...args);
template <typename T> data_t<T> make_data(DynamicArena &arena, auto &&...args);

template <typename D> using data_type = ...; // extracts T from data_t<T>
```

---

## Task ([guide](user_guide/task.md))

### Definition

```cpp
struct MyTask {
    required using inputs  = hh::type_list<A, B>;
    required using outputs = hh::type_list<C, D>;
    optional using node_input  = ...;  // default: LockQueueNodeInput
    optional using node_output = ...;  // default: EdgeSlot

    optional void initialize(optional auto ctx);
    optional void finalize(optional auto ctx);
    required void execute(optional auto ctx, data_t<A> data);  // one per input type; can be static
    optional std::shared_ptr<MyTask> copy();  // default: default-constructs
};
```

### Creation

```cpp
auto node = hh::make_task<MyTask>(size_t number_threads = 1, std::string name = "Task");
auto node = hh::make_task(std::make_shared<MyTask>(args...), number_threads, name);
```

### Lambda task

```cpp
auto node = hh::make_lambda_task<Sep, Types...>(number_threads = 1, name = "LambdaTask");
// Sep = number of input types in Types... (rest are outputs)
node->set_execute<T>(std::function<void(LambdaExecutionContext<T>*, data_t<T>)>);
```

### Execution context (`ctx`)

```cpp
ctx->name()           // std::string const&   — node name
ctx->graph_name()     // std::string const&   — parent graph name
ctx->graph_id()       // int                  — parent graph id
ctx->number_thread()  // size_t               — thread count for this node
ctx->thread_index()   // size_t               — 0-based thread index
ctx->numa_id()        // int                  — NUMA node id (-1 if none)
ctx->device_id()      // int                  — CUDA device id
ctx->rank()           // int                  — graph start rank
ctx->info()           // RuntimeInfo const&   — full runtime info
ctx->profiler()       // Profiler&            — node profiler

ctx->push_data<T>(data_t<T>)    // send data back to this task's own input
ctx->push_result<T>(data_t<T>)  // send data to downstream nodes via output edges
```

### Node input types

| Type                                   | Description                                   |
|----------------------------------------|-----------------------------------------------|
| `LockQueueNodeInput<Inputs...>`        | Mutex queue + condition variable (default)    |
| `MoodycamelMPMCInput<Inputs...>`       | Lock-free MPMC queue + atomic trigger         |
| `DakingMPSCInput<Inputs...>`           | Lock-free MPSC queue + atomic trigger         |
| `GroupNodeInput<GroupSize, InputType>` | Partitions threads into groups with own input |

---

## Graph ([guide](user_guide/graph.md))

### Creation

```cpp
auto g = hh::make_graph<Sep, Types...>(std::string name = "Graph");
// Sep = number of input types in Types... (rest are outputs)

auto g = hh::make_graph<Sep, Types...>(std::shared_ptr<Runner> runner, name);
auto g = hh::make_serial_graph<Sep, Types...>(name);  // single-threaded, for debugging
```

### Lifecycle

```cpp
g->start(int rank = 0);     // build + launch
g->stop();                  // force stop (no drain)
```

### Data flow

```cpp
g->push_data<T>(data_t<T> data);         // feed data into graph
auto result = g->get_result();           // blocks, returns std::variant<data_t<OutputTypes>...>
g->eat_results(size_t count = 1);        // blocks, discards count results
```

### Edges

```cpp
g->draw_edges(sender, receiver);          // connect all matching types
g->draw_edge<T>(sender, receiver);        // connect specific type
g->draw_edge<T>(sender, receiver,         // custom edge
    [](auto receiver, data_t<T> data, RuntimeInfo const &info) -> bool {
        receiver->push_data(std::move(data), info);
        return true;  // false = skip runner notification
    });

g->connect_inputs(node);                  // graph inputs → node (all matching)
g->connect_input<T>(node);                // graph inputs → node (specific type)
g->connect_outputs(node);                 // node → graph outputs (all matching)
g->connect_output<T>(node);               // node → graph outputs (specific type)
```

### Inspection

```cpp
g->generate_dot_file(std::string filename);  // graphviz output
GraphView g->build_graph_view();             // type-erased topology snapshot
```

### Runners

| Type                                | Description                   |
|-------------------------------------|-------------------------------|
| `DefaultGraphRunner` (ThreadRunner) | One thread per node (default) |
| `SerialRunner`                      | All nodes on one thread       |
| `TBBRunner`                         | TBB-based task scheduler      |

---

## Execution pipeline ([guide](user_guide/execution_pipelines.md))

### Definition

```cpp
struct MyPipeline {
    required std::shared_ptr<GraphType> make_graph(size_t graph_index);
    required size_t send_to(data_t<T> const &data);  // one per input type
};
```

### Creation

```cpp
auto p = hh::make_pipeline<MyPipeline>(std::vector<PipelineInfo> configs, std::string name = "Pipeline");
auto p = hh::make_pipeline(std::make_shared<MyPipeline>(args...), configs, name);

// lambda variant
auto p = hh::make_lambda_pipeline(configs, make_graph_fn, name);
p->set_send_to<T>(std::function<size_t(data_t<T> const &)>);
```

### PipelineInfo

```cpp
struct PipelineInfo { int numa_id = -1; int device_id = 0; };
```

---

## State manager ([guide](user_guide/state_managers.md))

### Definition

```cpp
struct MyState : hh::MutexState {  // or custom Lockable, or no base (atomics)
    required using inputs  = hh::type_list<A, B>;
    required using outputs = hh::type_list<C>;

    required void execute(auto ctx, data_t<A> data);  // one per input type
};
```

Always single-threaded (`number_threads = 1`). Calling `copy()` is fatal.

### Creation

```cpp
auto sm = hh::make_state_manager(std::make_shared<MyState>(args...), std::string name = "StateManager");
```

### MutexState

```cpp
class MutexState { void lock(); void unlock(); };
```

Any class satisfying `Lockable` (has `lock()` + `unlock()`) works as a base.

---

## CUDA task ([guide](user_guide/cuda-task.md))

Requires `#define HH_USE_CUDA`.

```cpp
class CudaTask {
    CudaTask();
    explicit CudaTask(bool enable_peer_access);

    virtual void initialize_cuda();   // user override (called after stream creation)
    virtual void finalize_cuda();     // user override (called before stream destruction)

    cudaStream_t stream() const;
    bool enable_peer_access() const;
    bool has_peer_access(int device_id);
};

hh_check_cuda_error(err);  // macro, requires HH_ENABLE_CHECK_CUDA
```

---

## Memory pools ([guide](user_guide/memory_pools.md))

### Pool\<T\> (manual release)

```cpp
Pool<T>();
Pool<T>(int numa_id, int device_id = -1);

void fill(size_t count, auto &&...args);  // construct count elements in-place
T *allocate(bool wait = false);            // wait=false returns nullptr if empty
void release(T *data);                     // calls clean_memory() on T if available
```

### AutomaticPool\<T\> (shared_ptr auto-release)

```cpp
AutomaticPool<T>();
AutomaticPool<T>(int numa_id, int device_id = -1);

void fill(size_t count, auto &&...args);
std::shared_ptr<T> allocate(bool wait = false);
```

### Multi-type variants

```cpp
MultiPool<Types...>           // pool.fill<T>(), pool.allocate<T>(), pool.release<T>(ptr)
MultiAutomaticPool<Types...>  // pool.fill<T>(), pool.allocate<T>()
```

### HasCleanMemory

```cpp
concept HasCleanMemory = requires(T &t) { t.clean_memory(); };
```

Thread-safe (lock-free `IndexAllocator` internally).

---

## Allocators ([guide](user_guide/allocators.md))

### Arena (fixed-size bump allocator, move-only)

```cpp
Arena();
Arena(size_t capacity, int numa_id = -1);
Arena(std::byte *buffer, size_t size);      // non-owning
Arena(std::span<std::byte> buffer);         // non-owning

T *allocate<T>(size_t count = 1);
std::byte *allocate(size_t bytes, size_t alignment);
void deallocate(void *, size_t);            // no-op
void reset();

size_t bytes_allocated() const;
size_t bytes_available() const;
size_t capacity() const;
bool contains(void const *ptr) const;
```

### DynamicArena (growing multi-block, move-only)

```cpp
DynamicArena();
DynamicArena(size_t block_size, int numa_id = -1);
DynamicArena(size_t block_size, size_t out_band_threshold, int numa_id = -1);

T *allocate<T>(size_t count = 1);
std::byte *allocate(size_t bytes, size_t alignment);
void deallocate(void *, size_t);            // no-op
bool resize(void *ptr, size_t old_size, size_t new_size);  // last allocation only
void reset();
void free_all();

size_t num_blocks() const;
size_t num_out_band() const;
size_t total_bytes_capacity() const;
```

### Smart pointers

```cpp
std::shared_ptr<T>                       hh::make_shared(Arena &, Args &&...);
std::unique_ptr<T, ArenaDeleter>         hh::make_unique(Arena &, Args &&...);
std::shared_ptr<T>                       hh::make_shared(DynamicArena &, Args &&...);
std::unique_ptr<T, DynamicArenaDeleter>  hh::make_unique(DynamicArena &, Args &&...);
```

---

## Profiling ([guide](user_guide/profiling.md))

Requires `#define HH_ENABLE_PROFILING`. Optional: `#define HH_USE_NVTX`.

### Macros

```cpp
HH_PROFILE_REGION(profiler, "name") { ... }  // times the block
HH_NVTX_REGION(profiler, "name") { ... }     // NVTX region
HH_NVTX_MARKER(profiler, "name");            // NVTX marker
```

### Profiler (via `ctx->profiler()`)

```cpp
Profile *profile_region(std::string label);   // get/create region (cache in initialize)
Profile *profile_marker(std::string label);   // get/create marker
void add_string(std::string label, auto &&...args);
```

### Profile

```cpp
bool begin_region();   // start timing
bool end_region();     // stop timing + record
void mark();           // NVTX instant mark
```

### Report output

```cpp
void report_to_text(GraphViewNode const &view, ProfileMap const &profiles, std::ostream &os);
void graph_view_to_dot(GraphViewNode const &view, ProfileMap const &profiles, std::ostream &os);
void graph_view_to_dot(GraphViewNode const &view, std::ostream &os);  // topology only
std::string hh::format_duration(double ns);
```

---

## Graph analysis ([guide](user_guide/graph_analysis.md))

```cpp
GraphView gv = graph->build_graph_view();

// traversal — fn returns true to recurse into children
hh::traverse(GraphViewNode const &view, F &&fn, std::set<ViewKind> filter = {all});

// cycle detection — prints cycles, returns count
size_t hh::ga_cycle_detection(GraphView const &view);

// edge queries
void hh::ga_node_input_edges(GraphView const &, Node *, std::vector<GraphViewNode *> &);
void hh::ga_node_output_edges(GraphView const &, Node *, std::vector<GraphViewNode *> &);
```

### GraphViewNode

```cpp
enum class ViewKind { Node, Edge, Graph, Pipeline };
struct GraphViewNode {
    ViewKind kind; Node *node; GraphViewNode *parent;
    ViewEdge edge;          // kind == Edge: { sender, receiver, type_name }
    ViewGraph graph;        // kind == Graph: { input_edges, output_edges, nodes, edges }
    ViewPipeline pipeline;  // kind == Pipeline: { graphs, edges }
};
```

---

## Logging ([guide](user_guide/logging.md))

```cpp
hh::log::set_level(hh::log::LogLevel::{Info|Warning|Error});  // default: Warning

hh::log::info(auto &&...args);      // stdout, inactive by default
hh::log::warning(auto &&...args);   // stderr
hh::log::error(auto &&...args);     // stderr + increments counter
hh::log::fatal(auto &&...args);     // stderr + std::abort() [[noreturn]]

size_t hh::log::error_count();
```

All functions auto-capture source location. Output: `HH  LEVEL  file(line:col): message`.

---

## Info structs

```cpp
struct PipelineInfo { int numa_id = -1; int device_id = 0; };
struct GraphInfo    { std::string name; int id; };
struct NodeInfo     { std::string name; size_t number_threads; };
struct RunInfo      { int rank; PipelineInfo pipeline; };
struct RuntimeInfo  { NodeInfo const *node; GraphInfo const *graph; RunInfo run;
                      size_t thread_index = 0; Profiler *profiler; };
```
