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
#include <variant>

#include "helpers.hpp"

namespace hh {

class Node;

struct GraphViewNode;

struct ViewNode {};

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

struct GraphViewNode {
    Node *node = nullptr;
    std::variant<ViewNode, ViewEdge, ViewGraph, ViewPipeline> data;

    // factory methods /////////////////////////////////////////////////////////

    static GraphViewNode make_node(Node *n) {
        return {n, ViewNode{}};
    }

    static GraphViewNode make_edge(Node *sender, Node *receiver, std::string type_name) {
        return {nullptr, ViewEdge{sender, receiver, std::move(type_name)}};
    }

    static GraphViewNode make_graph(Node *n) {
        return {n, ViewGraph{}};
    }

    static GraphViewNode make_pipeline(Node *n) {
        return {n, ViewPipeline{}};
    }

    // tag & accessors /////////////////////////////////////////////////////////

    ViewKind kind() const { return static_cast<ViewKind>(data.index()); }

    bool is_node() const { return kind() == ViewKind::Node; }
    bool is_edge() const { return kind() == ViewKind::Edge; }
    bool is_graph() const { return kind() == ViewKind::Graph; }
    bool is_pipeline() const { return kind() == ViewKind::Pipeline; }

    ViewNode       &view_node()           { return std::get<ViewNode>(data); }
    ViewNode const &view_node()     const { return std::get<ViewNode>(data); }
    ViewEdge       &edge()           { return std::get<ViewEdge>(data); }
    ViewEdge const &edge()     const { return std::get<ViewEdge>(data); }
    ViewGraph       &graph()           { return std::get<ViewGraph>(data); }
    ViewGraph const &graph()     const { return std::get<ViewGraph>(data); }
    ViewPipeline       &pipeline()           { return std::get<ViewPipeline>(data); }
    ViewPipeline const &pipeline()     const { return std::get<ViewPipeline>(data); }

    std::vector<GraphViewNode> &children() {
        if (auto *g = std::get_if<ViewGraph>(&data)) return g->children;
        return std::get<ViewPipeline>(data).children;
    }

    std::vector<GraphViewNode> const &children() const {
        if (auto *g = std::get_if<ViewGraph>(&data)) return g->children;
        return std::get<ViewPipeline>(data).children;
    }
};

// visit ///////////////////////////////////////////////////////////////////////

template <typename ...Fs>
decltype(auto) visit(GraphViewNode const &node, Fs &&...fs) {
    return std::visit(overloaded{std::forward<Fs>(fs)...}, node.data);
}

// traverse ////////////////////////////////////////////////////////////////////
//
// Depth-first traversal. The callback receives each GraphViewNode and returns
// bool: true to recurse into children (for graph/pipeline), false to skip.
//

template <typename F>
void traverse(GraphViewNode const &node, F &&fn) {
    bool go_deeper = fn(node);
    if (go_deeper) {
        if (auto *g = std::get_if<ViewGraph>(&node.data)) {
            for (auto &child : g->children) traverse(child, fn);
        } else if (auto *p = std::get_if<ViewPipeline>(&node.data)) {
            for (auto &child : p->children) traverse(child, fn);
        }
    }
}

} // end namespace hh

#endif
