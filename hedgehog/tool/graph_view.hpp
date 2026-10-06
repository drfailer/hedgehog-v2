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

#ifndef HEDGEHOG_TOOL_GRAPH_VIEW_H
#define HEDGEHOG_TOOL_GRAPH_VIEW_H

#include <string>
#include <vector>

#include "helpers.hpp"

namespace hh {

class Node;

struct GraphViewNode;

// TODO: this data structure should probably be recursive (use GraphViewNode*
//       instead of Node* so that we can access to more information)

struct ViewEdge {
    Node *sender;
    Node *receiver;
    std::string type_name;
};

struct ViewGraph {
    std::vector<Node *> input_nodes;
    std::vector<Node *> output_nodes;
    std::vector<GraphViewNode> children;
};

struct ViewPipeline {
    std::vector<GraphViewNode> children;
};

enum class ViewKind { Node, Edge, Graph, Pipeline };

// TODO: we need a parent view node here as well
struct GraphViewNode {
    ViewKind     kind     = ViewKind::Node;
    Node        *node     = nullptr;
    ViewEdge     edge     = {};
    ViewGraph    graph    = {};
    ViewPipeline pipeline = {};

    // factory methods /////////////////////////////////////////////////////////

    static GraphViewNode make_node(Node *n) {
        GraphViewNode view;
        view.kind = ViewKind::Node;
        view.node = n;
        return view;
    }

    static GraphViewNode make_edge(Node *sender, Node *receiver, std::string type_name) {
        GraphViewNode view;
        view.kind = ViewKind::Edge;
        view.edge = ViewEdge{sender, receiver, std::move(type_name)};
        return view;
    }

    static GraphViewNode make_graph(Node *n) {
        GraphViewNode view;
        view.kind = ViewKind::Graph;
        view.node = n;
        return view;
    }

    static GraphViewNode make_pipeline(Node *n) {
        GraphViewNode view;
        view.kind = ViewKind::Pipeline;
        view.node = n;
        return view;
    }
};

// traverse ////////////////////////////////////////////////////////////////////
//
// Depth-first traversal. The callback receives each GraphViewNode and returns
// bool: true to recurse into children (for graph/pipeline), false to skip.
//

template <typename F>
void traverse(GraphViewNode const &view, F &&fn) {
    if (!fn(view)) return;
    switch (view.kind) {
    case ViewKind::Graph:    for (auto &child : view.graph.children) traverse(child, fn); break;
    case ViewKind::Pipeline: for (auto &child : view.pipeline.children) traverse(child, fn); break;
    default: break;
    }
}

} // end namespace hh

#endif
