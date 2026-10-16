# Graph Analysis

Hedgehog provides tools to inspect and analyze graph topology at runtime through
the `GraphView` data structure.

## Graph view

A `GraphView` is a type-erased snapshot of the graph topology. Since graph nodes
are heavily templated, the view provides a uniform structure for traversal and
analysis.

Build a view from a graph (after `start()` or after `build()`):

```cpp
#include <hedgehog/tool/graph_view.hpp>

auto view = graph->build_graph_view();
```

The view contains:
- `root` — the root graph node
- `all_nodes` — all task/state nodes
- `all_edges` — all edges (with sender, receiver, type name)
- `all_graphs` — all sub-graphs
- `all_pipelines` — all pipelines
- `node_map` — maps `Node*` to its `GraphViewNode*`

Each `GraphViewNode` has a `kind` field (`ViewKind::Node`, `ViewKind::Edge`,
`ViewKind::Graph`, `ViewKind::Pipeline`) that determines which fields are valid.

## Traversal

Depth-first traversal over the view:

```cpp
#include <hedgehog/tool/graph_view.hpp>

hh::traverse(*view.root, [](hh::GraphViewNode const &node) -> bool {
    if (node.kind == hh::ViewKind::Node) {
        std::cout << "node: " << node.node->name() << std::endl;
    }
    return true; // return true to recurse into children
});
```

The callback returns `bool`: `true` to recurse into children (for graph/pipeline
nodes), `false` to skip.

An optional filter parameter restricts which node kinds trigger the callback:

```cpp
hh::traverse(*view.root, callback, {hh::ViewKind::Node}); // nodes only
```

## Cycle detection

Detect cycles in the graph topology:

```cpp
#include <hedgehog/tool/graph_analysis.hpp>

auto view = graph->build_graph_view();
size_t num_cycles = hh::ga_cycle_detection(view);
```

This prints each cycle found with the full path and returns the total count.
Useful for validating dynamically constructed graphs.

Output example:

```
cycle found at `TaskA`:  { TaskA -[int]-> TaskB -[int]-> TaskA }
```

## Helper functions

Query edges for a specific node:

```cpp
std::vector<hh::GraphViewNode *> in_edges, out_edges;
hh::ga_node_input_edges(view, node_ptr, in_edges);
hh::ga_node_output_edges(view, node_ptr, out_edges);
```

## Dot output

Generate a Graphviz dot file from a view (topology only, no profiling data):

```cpp
#include <hedgehog/tool/profiling_report.hpp>

hh::graph_view_to_dot(view, "graph.dot");
```

For profiling-annotated dot output, see [profiling](profiling.md).
