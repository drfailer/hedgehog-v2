# Execution Pipeline

Execution pipelines instantiate the same graph multiple times, one per
configuration entry (device + NUMA id). The `send_to` function determines which
graph instance processes each incoming data item.

## Pipeline definition

```cpp
struct MyPipeline {
    // called once per config entry — graph_index matches the position in the
    // config vector (0 for the first entry, 1 for the second, etc.)
    auto make_graph(size_t graph_index) {
        auto graph = hh::make_graph<2, int, float, int, float>("Worker");
        // create nodes, draw edges, ...
        return graph;
    }

    // called for every incoming data item to route it to the right graph.
    // must be implemented for every input type (can be templated as shown below)
    template <typename T>
    size_t send_to(hh::data_t<T> data) {
        return data->graph_index;
    }
};
```

## Creating a pipeline

```cpp
// config vector: one entry per graph instance
std::vector<hh::PipelineInfo> config = {
    {.numa_id = 0, .device_id = 0},  // index 0
    {.numa_id = 0, .device_id = 1},  // index 1
};

// default-construct MyPipeline
auto node = hh::make_pipeline<MyPipeline>(config, "My Pipeline");

// or construct MyPipeline manually
auto node = hh::make_pipeline(std::make_shared<MyPipeline>(args...), config, "My Pipeline");
```

## Wiring a pipeline in a graph

A pipeline is a node — it can be wired into a graph just like a task. Output
edges from the pipeline's sub-graphs are automatically connected to downstream
nodes in the parent graph.

```cpp
auto graph = hh::make_graph<2, int, float, int, float>("Main");
auto pipeline = hh::make_pipeline<MyPipeline>(config, "Pipeline");
auto sink = hh::make_node<MySink>("Sink");

graph->inputs(pipeline);
graph->edges(pipeline, sink);
graph->outputs(sink);
```

When a task inside a pipeline sub-graph calls `push_result`, the data flows to
whatever is connected downstream of the pipeline node in the parent graph. All
sub-graphs share the same output edges, so results from any graph instance
reach the same downstream nodes.

## Lambda interface

Like tasks, pipelines have a lambda interface. This example also shares a state
across the graph instances:

```cpp
auto state = std::make_shared<MyState>(...);

auto pipeline = hh::make_lambda_pipeline({{0, 0}, {0, 1}}, [state](size_t graph_index) {
    auto graph = hh::make_graph<2, int, float, int, float>("Worker");

    // share state across all graph instances via a state manager
    auto sm = hh::make_state_manager(state, "State");

    auto task = hh::make_node<MyTask>("Task");
    graph->inputs(sm);
    graph->edges(sm, task);
    graph->outputs(task);

    return graph;
});

// set send_to for each input type
pipeline->set_send_to<int>([](auto data) -> size_t {
    return data->graph_index;
});
pipeline->set_send_to<float>([](auto data) -> size_t {
    return data->graph_index;
});
```
