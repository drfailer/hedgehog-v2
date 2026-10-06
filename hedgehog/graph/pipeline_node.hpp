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

#ifndef HEDGEHOG_GRAPH_PIPELINE_NODE_H
#define HEDGEHOG_GRAPH_PIPELINE_NODE_H

#include <vector>

#include "node.hpp"

namespace hh {

template <typename Config>
struct PipelineNode : Node {
    // config //////////////////////////////////////////////////////////////////

    using InputTypes = Config::InputTypes;
    using OutputTypes = Config::OutputTypes;
    using Pipeline = Config::Pipeline;
    using GraphType = Config::GraphType;

    // attributes & constructors ///////////////////////////////////////////////

    std::shared_ptr<Pipeline> pipeline_ = nullptr;
    std::vector<std::shared_ptr<GraphType>> graphs_ = {};
    std::vector<PipelineInfo> configs_ = {};

    PipelineNode(std::shared_ptr<Pipeline> pipeline, NodeInfo const &info, std::vector<PipelineInfo> const &configs)
        : Node(info), pipeline_(std::move(pipeline)), graphs_(configs.size(), nullptr), configs_(configs) {
        for (size_t i = 0; i < configs_.size(); ++i) {
            graphs_[i] = pipeline_->make_graph(i);
        }
    }

    std::shared_ptr<Pipeline> pipeline() { return pipeline_; }
    std::vector<std::shared_ptr<GraphType>> graphs() { return graphs_; }
    std::vector<PipelineInfo> configs() { return configs_; }

    // node api ////////////////////////////////////////////////////////////////

    void initialize(GraphInfo const &info) override {
        for (auto &graph : graphs_) {
            graph->initialize(info);
        }
    }

    void run(RunInfo const &info) override {
        auto run_info = info;

        for (size_t i = 0; i < graphs_.size(); ++i) {
            run_info.pipeline = configs_[i];
            graphs_[i]->run(run_info);
        }
    }

    void finalize(GraphInfo const &info) override {
        for (auto &graph : graphs_) {
            graph->finalize(info);
        }
    }

    void profile(ProfileMap &map) override {
        Node::profile(map);
        for (auto &graph : graphs_) {
            graph->profile(map);
        }
    }

    GraphViewNode *graph_view(GraphView *gv) override {
        auto &arena = gv->arena;
        auto &map = gv->node_map;

        auto *view = GraphViewNode::make_pipeline(arena, this);
        map[this] = view;
        auto &children = view->pipeline.children;

        for (auto &graph : graphs_) {
            auto *child = graph->graph_view(gv);
            child->parent = view;

            type_list_map<InputTypes>([&]<typename T>() {
                auto const &edges = graph->input().template edges<T>();
                for (auto const &edge : edges) {
                    auto *receiver = reinterpret_cast<Node *>(edge.receiver);
                    if (auto it = map.find(receiver); it != map.end()) {
                        auto *edge_view = GraphViewNode::make_edge(arena, view, it->second, type_to_string<T>());
                        edge_view->parent = view;
                        children.push_back(edge_view);
                    }
                }
            });
            children.push_back(child);
        }
        return view;
    }

    // io //////////////////////////////////////////////////////////////////////

    template <typename T>
    void connect_output_edge(Edge<T> edge) {
        for (auto &graph : graphs_) {
            graph->connect_output_edge(edge);
        }
    }

    template <typename T>
    void push_data(data_t<T> data, RuntimeInfo const &info) {
        // FIXME: do we need infos in send_to? context?
        size_t graph_index = pipeline_->send_to(data);
        graphs_[graph_index]->push_data(std::move(data), info);
    }
};

} // end namespace hh

#endif
