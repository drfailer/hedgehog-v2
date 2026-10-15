# Execution Pipeline

Execution pipelines are used to create graphs that run on multiple GPU devices
and/or numa nodes (thread pinning). It effectively instanciate the same graph
multiple times for each configuration (device + numa id) and use the `send_to`
function to determin the index of the receiver graph.

## Pipeline definition

```cpp
struct MyPipeline {
    // The role of the make graph function is to create a graph for
    auto make_graph(size_t graph_index) {
        auto graph = hh::make_graph<2, int, float, int, float>();
        // create nodes, draw edges, ...
        return graph;
    }

    // send to must be implemented for every input type (it can also be
    // template as shown bellow). The input types are the same as the ones of
    // the graph returned by make_graph.

    template <typename T>
    size_t send_to(hh::data_t<T> data) {
        // determine which data belongs to which graph (in this case, we attach
        // the graph index to the data)
        return data->graph_idex;
    }
};
```

## Creating a pipeline

```cpp
// create a config in which we specify the numa id and device id
std::vector<hh::PipelineInfo> config = {
    {.numa_id = 0, .device_id = 0},
    {.numa_id = 0, .device_id = 1},
};

// create a pipeline (Pipeline default constructible)
auto node = hh::make_pipeline<MyPipeline>(config, "My Pipeline");

// create a pipeline and construct MyPipeline manually
auto node = hh::make_pipeline(std::make_shared<MyPipeline>(args...), config, "My Pipeline");
```

The created node can be wired in the main graph. It can be the only node or the
pipeline may act as a sub-graph (we can have multiple pipelines in the same
graph).

## Lambda interface

Like with tasks, there is a lambda interface for pipelines. In this example, we
also share a state across different graphs.

```cpp
// We have a state that will be shared accros all the graphs instances (locked
// by the state manager for thread safety)
auto state = std::make_shared<MyState>(...);

// create the lambda pipeline (we keep the same config as before and we capture the state)
auto pipeline = hh::make_lambda_pipeline({{0, 0}, {0, 1}}, [state](size_t graph_index) {
    auto graph = hh::make_graph<2, int, float, int, float>();
    auto sm = hh::make_state_manager(state, "State"); // will manage the state
    // create the graph...
    return graph;
});

// set the send to functions
pipeline->set_send_to<int>([](auto data) -> size_t {
    return data->graph_index;
});
pipeline->set_send_to<float>([](auto data) -> size_t {
    return data->graph_index;
});
```
