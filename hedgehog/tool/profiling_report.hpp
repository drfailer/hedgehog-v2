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

#ifndef HEDGEHOG_TOOL_PROFILING_REPORT_H
#define HEDGEHOG_TOOL_PROFILING_REPORT_H

#include <ostream>
#include <cstdio>
#include <algorithm>
#include <map>
#include "profiling.hpp"
#include "graph_view.hpp"
#include "../graph/node.hpp"

namespace hh {

using ProfileMap = std::map<uintptr_t, ProfileReport>;

inline uintptr_t node_id(Node *n) { return reinterpret_cast<uintptr_t>(n); }

// helpers /////////////////////////////////////////////////////////////////////

inline std::string html_escape(std::string const &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '&': out += "&amp;"; break;
        case '"': out += "&quot;"; break;
        case '\n': out += "<br/>"; break;
        default: out += c;
        }
    }
    return out;
}

inline std::string format_duration(double ns) {
    char buf[32];
    if (ns >= 1e9) {
        std::snprintf(buf, sizeof(buf), "%.3fs", ns / 1e9);
    } else if (ns >= 1e6) {
        std::snprintf(buf, sizeof(buf), "%.3fms", ns / 1e6);
    } else if (ns >= 1e3) {
        std::snprintf(buf, sizeof(buf), "%.3fus", ns / 1e3);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0fns", ns);
    }
    return buf;
}

inline std::string node_name(Node *n) {
    auto &info = n->info();
    if (info.number_threads > 1) {
        return info.name + " x" + std::to_string(info.number_threads);
    }
    return info.name;
}

// text output /////////////////////////////////////////////////////////////////

inline void report_to_text(GraphViewNode const &view, ProfileMap const &profiles, std::ostream &os, size_t indent = 0) {
    auto pad = std::string(indent * 2, ' ');

    switch (view.kind) {
    case ViewKind::Node:     os << pad << "node " << node_name(view.node) << ":\n"; break;
    case ViewKind::Edge:     os << pad << "edge " << view.edge.type_name << ":\n"; break;
    case ViewKind::Graph:    os << pad << "graph " << view.node->info().name << ":\n"; break;
    case ViewKind::Pipeline: os << pad << "pipeline " << view.node->info().name << ":\n"; break;
    }

    if (view.node) {
        auto it = profiles.find(node_id(view.node));
        if (it != profiles.end()) {
            for (auto &entry : it->second.entries) {
                switch (entry.kind) {
                case EntryKind::Region:
                    os << pad << "  " << entry.label << ": "
                       << format_duration(entry.region.measure.mean) << " +/- " << format_duration(entry.region.measure.stddev())
                       << " [" << format_duration(entry.region.measure.min) << "; " << format_duration(entry.region.measure.max) << "]"
                       << " (" << entry.region.measure.count << ")\n";
                    break;
                case EntryKind::String:
                    os << pad << "    ";
                    for (auto const &str : entry.string.values) { os << str << "<BR/>"; }
                    os << "\n";
                    break;
                }
            }
        }
    }

    switch (view.kind) {
    case ViewKind::Graph:
        for (auto *c : view.graph.nodes) report_to_text(*c, profiles, os, indent + 1);
        for (auto *c : view.graph.edges) report_to_text(*c, profiles, os, indent + 1);
        break;
    case ViewKind::Pipeline:
        for (auto *c : view.pipeline.graphs) report_to_text(*c, profiles, os, indent + 1);
        for (auto *c : view.pipeline.edges) report_to_text(*c, profiles, os, indent + 1);
        break;
    default: break;
    }
}

// dot output //////////////////////////////////////////////////////////////////

inline double compute_node_exec_time(ProfileReport const &report) {
    double total = 0;
    for (auto &entry : report.entries) {
        if (entry.kind != EntryKind::Region) continue;
        if (entry.label.starts_with("execute")) {
            total += entry.region.measure.mean * entry.region.measure.count;
        }
    }
    return total;
}

inline double find_max_exec(GraphViewNode const &view, ProfileMap const &profiles) {
    double max_exec = 0;
    traverse(view, [&](GraphViewNode const &n) -> bool {
        if (n.node) {
            auto it = profiles.find(node_id(n.node));
            if (it != profiles.end()) {
                max_exec = std::max(max_exec, compute_node_exec_time(it->second));
            }
        }
        return true;
    }, {ViewKind::Node});
    return max_exec;
}

inline std::string compute_node_color(Node *n, ProfileMap const &profiles, double max_exec) {
    auto it = profiles.find(node_id(n));
    if (it != profiles.end()) {
        double exec_time = compute_node_exec_time(it->second);
        double ratio = max_exec > 0 ? exec_time / max_exec : 0;
        int pos = std::clamp(int(ratio * 255), 0, 255);
        char buf[8];
        std::snprintf(buf, sizeof(buf), "#%02x00%02x", pos, 255 - pos);
        return buf;
    }
    return "#c0c0c0";
}

inline void write_node_label(std::ostream &os, std::string const &label,
                             ProfileEntries const &entries,
                             std::string const &bgcolor = "") {
    os << "<table border=\"0\" cellborder=\"1\" cellspacing=\"0\" cellpadding=\"5\">\n";
    auto escaped_label = html_escape(label);
    if (bgcolor.empty()) {
        os << "<tr><td colspan=\"2\"><b>" << escaped_label << "</b></td></tr>\n";
    } else {
        os << "<tr><td colspan=\"2\" bgcolor=\"" << bgcolor
           << "\"><font color=\"white\"><b>" << escaped_label << "</b></font></td></tr>\n";
    }
    for (auto &entry : entries) {
        os << "<tr><td align=\"left\">" << html_escape(entry.label) << "</td><td align=\"left\">";
        switch (entry.kind) {
        case EntryKind::Region: {
            auto &m = entry.region.measure;
            os << "avg: " << format_duration(m.mean) << " +/- " << format_duration(m.stddev())
               << " | ttl: " << format_duration(m.mean * m.count)
               << " | min: " << format_duration(m.min)
               << " - max: " << format_duration(m.max)
               << " | count: " << m.count;
        } break;
        case EntryKind::String: {
            for (auto const &str : entry.string.values) {
                os << html_escape(str) << "<BR/>";
            }
        } break;
        }
        os << "</td></tr>\n";
    }
    os << "</table>";
}

inline void write_view_label(std::ostream &os, std::string const &label) {
    os << "<table border=\"0\" cellborder=\"1\" cellspacing=\"0\" cellpadding=\"5\">\n";
    os << "<tr><td><b>" << html_escape(label) << "</b></td></tr>\n";
    os << "</table>";
}

inline void graph_view_content_to_dot(std::ostream &os, GraphViewNode const &view,
                                       ProfileMap const &profiles, double max_exec,
                                       size_t &edge_counter) {
    switch (view.kind) {
    case ViewKind::Node: {
        auto id = node_id(view.node);
        auto name = node_name(view.node);
        auto it = profiles.find(id);
        if (it != profiles.end()) {
            auto color = compute_node_color(view.node, profiles, max_exec);
            os << "node_" << id << " [shape=none, margin=0, label=<";
            write_node_label(os, name, it->second.entries, color);
            os << ">];\n";
        } else {
            os << "node_" << id << " [shape=none, margin=0, label=<";
            write_view_label(os, name);
            os << ">];\n";
        }
    } break;
    case ViewKind::Edge: {
        using namespace std::string_literals;
        auto sender = "node_"s + std::to_string(node_id(view.edge.sender->node));
        auto receiver = "node_"s + std::to_string(node_id(view.edge.receiver->node));
        auto edge = "edge_"s + std::to_string(edge_counter++);

        os << sender << " -> " << edge << " [dir=none];\n";
        os << edge << "[shape=rect, style=filled, fillcolor=\"#ffffff\", label=\"" << view.edge.type_name << "\"];\n";
        os << edge << " -> " << receiver << ";\n";
    } break;
    case ViewKind::Graph: {
        auto id = node_id(view.node);
        auto name = view.node->info().name;
        os << "subgraph cluster_" << id << " {\n";
        os << "label=\"" << name << "\"; fontsize=25; penwidth=5; labelloc=top; labeljust=left;\n";
        os << "style=filled;\n";
        os << "fillcolor=\"#ffffff\";\n";
        for (auto *n : view.graph.nodes) {
            graph_view_content_to_dot(os, *n, profiles, max_exec, edge_counter);
        }
        for (auto *e : view.graph.edges) {
            graph_view_content_to_dot(os, *e, profiles, max_exec, edge_counter);
        }
        os << "}\n";
    } break;
    case ViewKind::Pipeline: {
        auto id = node_id(view.node);
        auto name = view.node->info().name;
        os << "subgraph cluster_" << id << " {\n";
        os << "style=filled;\n";
        os << "fillcolor=\"#e8e8e8\";\n";
        os << "label=\"" << name << "\"; fontsize=25; penwidth=5; labelloc=top; labeljust=left;\n";
        os << "node_" << id
           << " [label=\"\", shape=diamond, width=.3, style=filled, fillcolor=\"#606060\"];\n";
        for (auto *e : view.pipeline.edges) {
            graph_view_content_to_dot(os, *e, profiles, max_exec, edge_counter);
        }
        for (auto *g : view.pipeline.graphs) {
            graph_view_content_to_dot(os, *g, profiles, max_exec, edge_counter);
        }
        os << "}\n";
    } break;
    }
}

inline void graph_view_to_dot(GraphViewNode const &view, ProfileMap const &profiles, std::ostream &os) {
    double max_exec = find_max_exec(view, profiles);
    size_t edge_counter = 0;
    os << "digraph {\n";
    os << "rankdir=TB;\n";
    os << "labelloc=tl;\n";

    auto id = node_id(view.node);
    auto name = view.node->info().name;

    auto it = profiles.find(id);
    if (it != profiles.end()) {
        os << "label=<";
        write_node_label(os, name, it->second.entries);
        os << ">; fontsize=25; penwidth=5; labelloc=top; labeljust=left;\n";
    } else {
        os << "label=\"" << name << "\"; fontsize=25; penwidth=5; labelloc=top; labeljust=left;\n";
    }

    auto &g = view.graph;
    os << "node_" << id << " [label=\"\", width=.1, shape=circle];\n";
    auto sink_name = "sink_" + std::to_string(id);
    if (!g.output_edges.empty()) {
        os << sink_name << " [label=\"\", width=.1, shape=point];\n";
    }
    for (auto *n : g.nodes) {
        graph_view_content_to_dot(os, *n, profiles, max_exec, edge_counter);
    }
    for (auto *e : g.edges) {
        graph_view_content_to_dot(os, *e, profiles, max_exec, edge_counter);
    }
    for (auto &oe : g.output_edges) {
        using namespace std::string_literals;
        auto sender = "node_"s + std::to_string(node_id(oe.sender->node));
        auto edge = "edge_"s + std::to_string(edge_counter++);
        os << sender << " -> " << edge << " [dir=none];\n";
        os << edge << "[shape=rect, style=filled, fillcolor=\"#ffffff\", label=\"" << oe.type_name << "\"];\n";
        os << edge << " -> " << sink_name << ";\n";
    }
    os << "}\n";
}

inline void graph_view_to_dot(GraphViewNode const &view, std::ostream &os) {
    ProfileMap empty;
    graph_view_to_dot(view, empty, os);
}

} // end namespace hh

#endif
