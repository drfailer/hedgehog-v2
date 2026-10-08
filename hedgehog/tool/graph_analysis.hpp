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

#ifndef HEDGEHOG_TOOL_GRAPH_ANALYSIS_H
#define HEDGEHOG_TOOL_GRAPH_ANALYSIS_H

#include <vector>
#include <set>
#include <map>
#include <cstdio>

#include "graph_view.hpp"
#include "../graph/node.hpp"

namespace hh {

inline void ga_node_input_edges(GraphView const &view, Node *node, std::vector<GraphViewNode *> &edges) {
    for (auto edge : view.all_edges) {
        if (edge->edge.receiver->node == node) {
            edges.push_back(edge);
        }
    }
}

inline void ga_node_output_edges(GraphView const &view, Node *node, std::vector<GraphViewNode *> &edges) {
    for (auto edge : view.all_edges) {
        if (edge->edge.sender->node == node) {
            edges.push_back(edge);
        }
    }
}

struct CycleDetector {
    GraphView const             &view;
    std::map<GraphViewNode *, std::vector<GraphViewNode *>> nodes_output_edges;
    std::vector<GraphViewNode *> path;
    std::set<GraphViewNode *>    visited_nodes = {};
    size_t                       cycle_count = 0;

    CycleDetector(GraphView const &view) : view(view) {
        path.reserve(64);
        // collect output edges map
        for (auto node : view.all_nodes) {
            std::vector<GraphViewNode *> edges;
            ga_node_output_edges(view, node->node, edges);
            nodes_output_edges[node] = edges;
        }
    }

    size_t detect_cycles() {
        detect_cycles_rec(view.root);
        return cycle_count;
    }

  private:
    // recursive graph exploration (explore all the possible paths and print found cycles)
    void detect_cycles_rec(GraphViewNode *node) {
        switch (node->kind) {
        case ViewKind::Node:
            path.push_back(node);
            if (visited_nodes.contains(node)) {
                printf("cycle found at `%s`: ", node->node->name().c_str());
                bool printing_cycle = false;
                for (auto path_node : path) {
                    if (!printing_cycle && path_node == node) {
                        printing_cycle = true;
                        printf(" { ");
                    }
                    print_path_node(path_node);
                }
                printf(" }\n");
                cycle_count += 1;
            } else {
                visited_nodes.insert(node);
                auto const &output_edges = nodes_output_edges[node];
                for (auto edge : output_edges) {
                    detect_cycles_rec(edge);
                }
                visited_nodes.erase(node);
            }
            path.pop_back();
            break;
        case ViewKind::Edge:
            // add the edge to the path and explore the receiver
            path.push_back(node);
            detect_cycles_rec(node->edge.receiver);
            path.pop_back();
            break;
        case ViewKind::Graph:
            // explore all the possible paths starting from the graphs inputs
            for (auto edge : node->graph.input_edges) {
                detect_cycles_rec(edge.node);
            }
            printf("\n");
            break;
        case ViewKind::Pipeline:
            path.push_back(node);
            for (auto edge : node->pipeline.edges) {
                if (edge->edge.sender == node) { // input edges only
                    detect_cycles_rec(edge->edge.receiver);
                }
            }
            path.pop_back();
            break;
        }
    }

    void print_path_node(GraphViewNode const *node) {
        switch (node->kind) {
        case ViewKind::Edge: printf(" -[%s]-> ", node->edge.type_name.c_str()); break;
        case ViewKind::Node: /* fallthrough */
        case ViewKind::Graph: /* fallthrough */
        case ViewKind::Pipeline: /* fallthrough */
            printf("%s", node->node->name().c_str());
            break;
        }
    }
};

inline size_t ga_cycle_detection(GraphView const &view) {
    return CycleDetector(view).detect_cycles();
}

} // end namespace hh

#endif
