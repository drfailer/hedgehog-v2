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

namespace hh {

using ProfileMap = std::map<uintptr_t, ProfileReport>;

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

// text output /////////////////////////////////////////////////////////////////

inline void report_to_text(GraphViewNode const &view, ProfileMap const &profiles, std::ostream &os, size_t indent = 0) {
    auto pad = std::string(indent * 2, ' ');

    hh::visit(view,
        [&](ViewNode const &)     { os << pad << "node " << view.label << ":\n"; },
        [&](ViewEdge const &)     { os << pad << "edge " << view.label << ":\n"; },
        [&](ViewGraph const &)    { os << pad << "graph " << view.label << ":\n"; },
        [&](ViewPipeline const &) { os << pad << "pipeline " << view.label << ":\n"; }
    );

    auto it = profiles.find(view.id);
    if (it != profiles.end()) {
        for (auto &entry : it->second.entries) {
            std::visit(overloaded{
                [&](RegionEntry const &r) {
                    os << pad << "  " << entry.label << ": "
                       << format_duration(r.measure.mean) << " +/- " << format_duration(r.measure.stddev())
                       << " [" << format_duration(r.measure.min) << "; " << format_duration(r.measure.max) << "]"
                       << " (" << r.measure.count << ")\n";
                },
                [&](StringEntry const &s) {
                    os << pad << "    ";
                    for (auto const &str : s.values) { os << str << "<BR/>"; }
                    os << "\n";
                },
            }, entry.data);
        }
    }

    hh::visit(view,
        [&](ViewGraph const &g)    { for (auto &c : g.children) report_to_text(c, profiles, os, indent + 1); },
        [&](ViewPipeline const &p) { for (auto &c : p.children) report_to_text(c, profiles, os, indent + 1); },
        [](auto const &) {}
    );
}

// dot output //////////////////////////////////////////////////////////////////

inline double compute_node_exec_time(ProfileReport const &report) {
    double total = 0;
    for (auto &entry : report.entries) {
        if (auto *r = std::get_if<RegionEntry>(&entry.data);
            r && entry.label.starts_with("execute")) {
            total += r->measure.mean * r->measure.count;
        }
    }
    return total;
}

inline double find_max_exec(GraphViewNode const &view, ProfileMap const &profiles) {
    double max_exec = 0;
    traverse(view, [&](GraphViewNode const &node) -> bool {
        if (node.is_node()) {
            auto it = profiles.find(node.id);
            if (it != profiles.end()) {
                max_exec = std::max(max_exec, compute_node_exec_time(it->second));
            }
        }
        return true;
    });
    return max_exec;
}

inline std::string compute_node_color(uintptr_t id, ProfileMap const &profiles, double max_exec) {
    auto it = profiles.find(id);
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
        std::visit(overloaded{
            [&](RegionEntry const &r) {
                auto &m = r.measure;
                os << "avg: " << format_duration(m.mean) << " +/- " << format_duration(m.stddev())
                   << " | ttl: " << format_duration(m.mean * m.count)
                   << " | min: " << format_duration(m.min)
                   << " - max: " << format_duration(m.max)
                   << " | count: " << m.count;
            },
            [&](StringEntry const &s) {
                for (auto const &str : s.values) {
                    os << html_escape(str) << "<BR/>";
                }
            },
        }, entry.data);
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
                                       ProfileMap const &profiles, double max_exec) {
    hh::visit(view,
        [&](ViewNode const &) {
            auto it = profiles.find(view.id);
            if (it != profiles.end()) {
                auto color = compute_node_color(view.id, profiles, max_exec);
                os << "node_" << view.id << " [shape=none, margin=0, label=<";
                write_node_label(os, view.label, it->second.entries, color);
                os << ">];\n";
            } else {
                os << "node_" << view.id << " [shape=none, margin=0, label=<";
                write_view_label(os, view.label);
                os << ">];\n";
            }
        },
        [&](ViewEdge const &e) {
            using namespace std::string_literals;
            auto sender = "node_"s + std::to_string(e.sender_id);
            auto receiver = "node_"s + std::to_string(e.receiver_id);
            auto edge = "edge_"s + std::to_string(view.id) + "_"s
                      + std::to_string(e.sender_id) + "_"s + std::to_string(e.receiver_id);

            os << sender << " -> " << edge << " [dir=none];\n";
            os << edge << "[shape=rect, style=filled, fillcolor=\"#ffffff\", label=\"" << view.label << "\"];\n";
            os << edge << " -> " << receiver << ";\n";
        },
        [&](ViewGraph const &g) {
            os << "subgraph cluster_" << std::to_string(view.id) << " {\n";
            os << "label=\"" << view.label << "\"; fontsize=25; penwidth=5; labelloc=top; labeljust=left;\n";
            os << "style=filled;\n";
            os << "fillcolor=\"#ffffff\";\n";
            for (auto &child : g.children) {
                graph_view_content_to_dot(os, child, profiles, max_exec);
            }
            os << "}\n";
        },
        [&](ViewPipeline const &p) {
            os << "subgraph cluster_" << std::to_string(view.id) << " {\n";
            os << "style=filled;\n";
            os << "fillcolor=\"#e8e8e8\";\n";
            os << "label=\"" << view.label << "\"; fontsize=25; penwidth=5; labelloc=top; labeljust=left;\n";
            os << "node_" << view.id
               << " [label=\"\", shape=diamond, width=.3, style=filled, fillcolor=\"#606060\"];\n";
            for (auto &child : p.children) {
                graph_view_content_to_dot(os, child, profiles, max_exec);
            }
            os << "}\n";
        }
    );
}

inline void graph_view_to_dot(GraphViewNode const &view, ProfileMap const &profiles, std::ostream &os) {
    double max_exec = find_max_exec(view, profiles);
    os << "digraph {\n";
    os << "rankdir=TB;\n";
    os << "labelloc=tl;\n";

    auto it = profiles.find(view.id);
    if (it != profiles.end()) {
        os << "label=<";
        write_node_label(os, view.label, it->second.entries);
        os << ">; fontsize=25; penwidth=5; labelloc=top; labeljust=left;\n";
    } else {
        os << "label=\"" << view.label << "\"; fontsize=25; penwidth=5; labelloc=top; labeljust=left;\n";
    }

    auto &g = std::get<ViewGraph>(view.data);
    os << "node_" << std::to_string(g.source_id) << " [label=\"\", width=.1, shape=circle];\n";
    os << "node_" << std::to_string(g.sink_id) << " [label=\"\", width=.1, shape=point];\n";
    for (auto &child : g.children) {
        graph_view_content_to_dot(os, child, profiles, max_exec);
    }
    os << "}\n";
}

inline void graph_view_to_dot(GraphViewNode const &view, std::ostream &os) {
    ProfileMap empty;
    graph_view_to_dot(view, empty, os);
}

} // end namespace hh

#endif
