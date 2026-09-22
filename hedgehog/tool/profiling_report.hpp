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
#include "profiling.hpp"

namespace hh {

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

inline void report_to_text(ProfilerReport const &report, std::ostream &os, size_t indent = 0) {
    auto pad = std::string(indent * 2, ' ');
    switch (report.kind) {
    case ProfileReportKind::Node:     os << pad << "node " << report.label << ":\n"; break;
    case ProfileReportKind::Edge:     os << pad << "edge:\n"; break;
    case ProfileReportKind::Graph:    os << pad << "graph " << report.label << ":\n"; break;
    case ProfileReportKind::Pipeline: os << pad << "pipeline " << report.label << ":\n"; break;
    }
    for (auto &[label, profile] : report.profiles) {
        if (profile.has_region()) {
            auto &m = profile.region.measure;
            os << pad << "  " << label << ": "
               << format_duration(m.mean) << " +/- " << format_duration(m.stddev())
               << " [" << format_duration(m.min) << "; " << format_duration(m.max) << "]"
               << " (" << m.count << ")\n";
        }
        if (profile.has_info()) {
            os << pad << "    " << profile.info << "\n";
        }
    }
    for (auto &child : report.children) {
        report_to_text(child, os, indent + 1);
    }
}

// dot output //////////////////////////////////////////////////////////////////

inline double compute_node_exec_time(ProfilerReport const &report) {
    double total = 0;
    for (auto &[name, profile] : report.profiles) {
        if (name.starts_with("execute") && profile.has_region()) {
            total += profile.region.measure.mean * profile.region.measure.count;
        }
    }
    return total;
}

inline double find_max_exec(ProfilerReport const &report) {
    double max_exec = 0;
    for (auto &child : report.children) {
        if (child.kind == ProfileReportKind::Node) {
            max_exec = std::max(max_exec, compute_node_exec_time(child));
        } else if (child.kind == ProfileReportKind::Graph
                || child.kind == ProfileReportKind::Pipeline) {
            max_exec = std::max(max_exec, find_max_exec(child));
        }
    }
    return max_exec;
}

inline std::string compute_node_color(ProfilerReport const &node, double max_exec) {
    double exec_time = compute_node_exec_time(node);
    double ratio = max_exec > 0 ? exec_time / max_exec : 0;
    int pos = std::clamp(int(ratio * 255), 0, 255);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x00%02x", pos, 255 - pos);
    return buf;
}

inline void write_node_label(std::ostream &os, ProfilerReport const &report, std::string const &bgcolor = "") {
    os << "<table border=\"0\" cellborder=\"1\" cellspacing=\"0\" cellpadding=\"5\">\n";
    auto escaped_label = html_escape(report.label);
    if (bgcolor.empty()) {
        os << "<tr><td colspan=\"2\"><b>" << escaped_label << "</b></td></tr>\n";
    } else {
        os << "<tr><td colspan=\"2\" bgcolor=\"" << bgcolor
           << "\"><font color=\"white\"><b>" << escaped_label << "</b></font></td></tr>\n";
    }
    for (auto &[name, profile] : report.profiles) {
        if (!profile.has_region() && !profile.has_info()) continue;
        os << "<tr><td align=\"left\">" << html_escape(name) << "</td><td align=\"left\">";
        if (profile.has_region()) {
            auto &m = profile.region.measure;
            os << "avg: " << format_duration(m.mean) << " +/- " << format_duration(m.stddev())
               << " | ttl: " << format_duration(m.mean * m.count)
               << " | min: " << format_duration(m.min)
               << " - max: " << format_duration(m.max)
               << " | count: " << m.count;
        }
        if (profile.has_info()) {
            if (profile.has_region()) os << "<br/>";
            os << html_escape(profile.info);
        }
        os << "</td></tr>\n";
    }
    os << "</table>";
}

inline void report_content_to_dot(std::ostream &os, ProfilerReport const &report, double max_exec) {
    switch (report.kind) {
    case ProfileReportKind::Node: {
        auto color = compute_node_color(report, max_exec);
        os << "node_" << report.id << " [shape=none, margin=0, label=<";
        write_node_label(os, report, color);
        os << ">];\n";
    } break;
    case ProfileReportKind::Edge: {
        using namespace std::string_literals; // for ""s

        auto sender = "node_"s + std::to_string(report.sender_id);
        auto receiver = "node_"s + std::to_string(report.receiver_id);
        auto edge = "edge_"s + std::to_string(report.id) + "_"s + std::to_string(report.sender_id) + "_"s + std::to_string(report.receiver_id);

        os << sender << " -> " << edge << " [dir=none];\n";
        os << edge << "[shape=rect, style=filled, fillcolor=\"#ffffff\", label=\"" << report.label << "\"];\n";
        os << edge << " -> " << receiver << ";\n";
    } break;
    case ProfileReportKind::Graph: {
        os << "subgraph cluster_" << std::to_string(report.id) << " {\n";
        os << "label=\"" << report.label << "\"; fontsize=25; penwidth=5; labelloc=top; labeljust=left;\n";
        os << "style=filled;\n";
        os << "fillcolor=\"#ffffff\";\n";
        for (auto child : report.children) {
            report_content_to_dot(os, child, max_exec);
        }
        os << "}\n";
    } break;
    case ProfileReportKind::Pipeline: {
        using namespace std::string_literals;

        os << "subgraph cluster_" << std::to_string(report.id) << " {\n";
        os << "style=filled;\n";
        os << "fillcolor=\"#e8e8e8\";\n";
        os << "label=\"" << report.label << "\"; fontsize=25; penwidth=5; labelloc=top; labeljust=left;\n";

        os << "node_" << report.id
           << " [label=\"\", shape=diamond, width=.3, style=filled, fillcolor=\"#606060\"];\n";

        for (auto &child : report.children) {
            report_content_to_dot(os, child, max_exec);
        }
        os << "}\n";
    } break;
    }
}

inline void report_to_dot(ProfilerReport const &report, std::ostream &os) {
    double max_exec = find_max_exec(report);
    os << "digraph {\n";
    os << "rankdir=TB;\n";
    os << "labelloc=tl;\n";
    os << "label=<";
    write_node_label(os, report);
    os << ">; fontsize=25; penwidth=5; labelloc=top; labeljust=left;\n";
    os << "node_" << std::to_string(report.sender_id) << " [label=\"\", width=.1, shape=circle];\n";
    os << "node_" << std::to_string(report.receiver_id) << " [label=\"\", width=.1, shape=point];\n";
    for (auto child : report.children) {
        report_content_to_dot(os, child, max_exec);
    }
    os << "}\n";
}

} // end namespace hh

#endif
