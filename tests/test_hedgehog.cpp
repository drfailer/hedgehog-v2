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


#include <gtest/gtest.h>
#include <cstdio>
#include <algorithm>
#include "../hedgehog/hedgehog.h"

struct Task {
    using inputs = hh::type_list<int, float>;
    using outputs = hh::type_list<int, float>;

    void execute(auto ctx, hh::data_t<int> data) {
        printf("%s::execute<int>(%d)[%ld]\n", ctx->name().c_str(), *data, ctx->thread_index());
        ctx->push_result(data);
    }

    void execute(auto ctx, hh::data_t<float> data) {
        printf("%s::execute<float>(%f)[%ld]\n", ctx->name().c_str(), *data, ctx->thread_index());
        ctx->push_result(data);
    }
};

//
// Make sure that the we can create tasks and connect them to the graph. Also
// test that the thread executor runs the tasks, that the data flows correctly
// through the graph and that the graph terminates correctly. This test also
// generates a dot file which contains execution information when the profiling
// is enabled.
//
TEST(graph, simple) {
    auto node1 = hh::make_task<Task>(2, "task1");
    auto node2 = hh::make_task<Task>(2, "task2");
    auto graph = hh::make_graph<2, int, float, int, float>();

    graph->connect_inputs(node1);
    graph->draw_edges(node1, node2);
    graph->connect_outputs(node2);

    graph->start();
    graph->push_data(hh::make_data<float>(3.14));
    graph->push_data(hh::make_data<int>(4));
    auto test_value = [&](auto value) {
        using value_type = decltype(value);
        printf("value_type = %s\n", hh::type_to_string<value_type>().c_str());
        if constexpr (std::is_same_v<value_type, std::shared_ptr<int>>) {
            printf("value received %d\n", *value);
            ASSERT_EQ(*value, 4) << "int received";
        } else if constexpr (std::is_same_v<value_type, std::shared_ptr<float>>) {
            printf("value received %f\n", *value);
            ASSERT_EQ(*value, 3.14f) << "float received";
        }
    };
    std::visit(test_value, graph->get_result());
    std::visit(test_value, graph->get_result());
    graph->stop();

    graph->generate_dot_file("simple.dot");
}

//
// Test that we can connect sub-graphs and that the graph runs well when doing
// so.
//
TEST(graph, sub_graph) {
    auto inner1 = hh::make_task<Task>(1, "inner1");
    auto inner2 = hh::make_task<Task>(1, "inner2");

    auto subgraph = hh::make_graph<2, int, float, int, float>("subgraph");
    subgraph->connect_inputs(inner1);
    subgraph->draw_edges(inner1, inner2);
    subgraph->connect_outputs(inner2);

    auto outer_in = hh::make_task<Task>(1, "outer_in");
    auto graph = hh::make_graph<2, int, float, int, float>("main");
    graph->connect_inputs(outer_in);
    graph->draw_edges(outer_in, subgraph);
    graph->connect_outputs(subgraph);

    graph->start();
    graph->push_data(hh::make_data<int>(42));
    graph->push_data(hh::make_data<float>(2.71f));

    auto check = [](auto value) {
        using V = decltype(value);
        if constexpr (std::is_same_v<V, std::shared_ptr<int>>) {
            EXPECT_EQ(*value, 42);
        } else if constexpr (std::is_same_v<V, std::shared_ptr<float>>) {
            EXPECT_FLOAT_EQ(*value, 2.71f);
        }
    };
    std::visit(check, graph->get_result());
    std::visit(check, graph->get_result());
    graph->stop();

    graph->generate_dot_file("sub_graph.dot");
}

//
// Test that nested sub-graphs works properly.
//
TEST(graph, nested_sub_graph) {
    auto inner1 = hh::make_task<Task>(1, "inner1");
    auto inner2 = hh::make_task<Task>(1, "inner2");

    auto subgraph1 = hh::make_graph<2, int, float, int, float>("subgraph1");
    subgraph1->connect_inputs(inner1);
    subgraph1->draw_edges(inner1, inner2);
    subgraph1->connect_outputs(inner2);

    auto subgraph2 = hh::make_graph<2, int, float, int, float>("subgraph2");
    subgraph2->connect_inputs(subgraph1);
    subgraph2->connect_outputs(subgraph1);

    auto outer_in = hh::make_task<Task>(1, "outer_in");
    auto outer_out = hh::make_task<Task>(1, "outer_out");
    auto graph = hh::make_graph<2, int, float, int, float>("main");
    graph->connect_inputs(outer_in);
    graph->draw_edges(outer_in, subgraph2);
    graph->draw_edges(subgraph2, outer_out);
    graph->connect_outputs(outer_out);

    graph->start();
    graph->push_data(hh::make_data<int>(42));
    graph->push_data(hh::make_data<float>(2.71f));

    auto check = [](auto value) {
        using V = decltype(value);
        if constexpr (std::is_same_v<V, std::shared_ptr<int>>) {
            EXPECT_EQ(*value, 42);
        } else if constexpr (std::is_same_v<V, std::shared_ptr<float>>) {
            EXPECT_FLOAT_EQ(*value, 2.71f);
        }
    };
    std::visit(check, graph->get_result());
    std::visit(check, graph->get_result());
    graph->stop();

    graph->generate_dot_file("nested_sub_graph.dot");
}

struct State : hh::MutexState {
    using inputs = hh::type_list<int, float>;
    using outputs = hh::type_list<int, float>;

    int counter = 0;
    size_t int_count = 0;
    size_t float_count = 0;

    State(int counter) : counter(counter) {}

    void execute(auto ctx, hh::data_t<int> data) {
        printf("%s::execute<int>(%d)[%ld]\n", ctx->name().c_str(), *data, ctx->thread_index());
        int_count += 1;
        counter += *data;
        ctx->push_result(data);
    }

    void execute(auto ctx, hh::data_t<float> data) {
        printf("%s::execute<float>(%f)[%ld]\n", ctx->name().c_str(), *data, ctx->thread_index());
        float_count += 1;
        counter = static_cast<int>(static_cast<float>(counter) * *data);
        ctx->push_result(data);
    }
};

//
// Make sure the state manager compile and that we are able to share a state
// between multiple state managers within the same graph.
//
TEST(graph, state_manager) {
    auto graph = hh::make_graph<2, int, float, int, float>();
    auto state = std::make_shared<State>(4);
    auto sm1 = hh::make_state_manager(state, "sm1");
    auto sm2 = hh::make_state_manager(state, "sm2");

    graph->connect_inputs(sm1);
    graph->draw_edges(sm1, sm2);
    graph->connect_outputs(sm2);

    graph->start();
    graph->push_data(hh::make_data<int>(4));
    graph->eat_results();
    EXPECT_EQ(state->int_count, 2);
    EXPECT_EQ(state->float_count, 0);
    EXPECT_EQ(state->counter, 12);
    graph->push_data(hh::make_data<float>(0.5f));
    graph->eat_results();
    EXPECT_EQ(state->int_count, 2);
    EXPECT_EQ(state->float_count, 2);
    EXPECT_EQ(state->counter, 3);
    graph->stop();
}

//
// Make sure the serial executor is able to run sub-graphs.
//
TEST(serial_executor, sub_graph) {
    auto inner1 = hh::make_task<Task>(1, "inner1");
    auto inner2 = hh::make_task<Task>(1, "inner2");

    auto subgraph = hh::make_serial_graph<2, int, float, int, float>("subgraph");
    subgraph->connect_inputs(inner1);
    subgraph->draw_edges(inner1, inner2);
    subgraph->connect_outputs(inner2);

    auto outer_in = hh::make_task<Task>(1, "outer_in");
    auto graph = hh::make_serial_graph<2, int, float, int, float>("main");
    graph->connect_inputs(outer_in);
    graph->draw_edges(outer_in, subgraph);
    graph->connect_outputs(subgraph);

    graph->start();
    graph->push_data(hh::make_data<int>(42));
    graph->push_data(hh::make_data<float>(2.71f));

    auto check = [](auto value) {
        using V = decltype(value);
        if constexpr (std::is_same_v<V, std::shared_ptr<int>>) {
            EXPECT_EQ(*value, 42);
        } else if constexpr (std::is_same_v<V, std::shared_ptr<float>>) {
            EXPECT_FLOAT_EQ(*value, 2.71f);
        }
    };
    std::visit(check, graph->get_result());
    std::visit(check, graph->get_result());
    graph->stop();
}

//
// Test the lambda task interface.
//
TEST(lambda_task, simple) {
    auto node = hh::make_lambda_task<2, int, float, int, float>(2, "lambda_task");
    auto graph = hh::make_graph<2, int, float, int, float>();

    node->set_execute<int>([](auto ctx, auto data) {
        printf("%s::execute<int>(%d)[%ld]\n", ctx->name().c_str(), *data, ctx->thread_index());
        ctx->push_result(data);
    });
    node->set_execute<float>([](auto ctx, auto data) {
        printf("%s::execute<float>(%f)[%ld]\n", ctx->name().c_str(), *data, ctx->thread_index());
        ctx->push_result(data);
    });

    graph->connect_inputs(node);
    graph->connect_outputs(node);

    graph->start();
    graph->push_data(hh::make_data<float>(3.14));
    graph->push_data(hh::make_data<int>(4));
    auto test_value = [&](auto value) {
        using value_type = decltype(value);
        printf("value_type = %s\n", hh::type_to_string<value_type>().c_str());
        if constexpr (std::is_same_v<value_type, std::shared_ptr<int>>) {
            printf("value received %d\n", *value);
            ASSERT_EQ(*value, 4) << "int received";
        } else if constexpr (std::is_same_v<value_type, std::shared_ptr<float>>) {
            printf("value received %f\n", *value);
            ASSERT_EQ(*value, 3.14f) << "float received";
        }
    };
    std::visit(test_value, graph->get_result());
    std::visit(test_value, graph->get_result());
    graph->stop();

    graph->generate_dot_file("lambda_simple.dot");
}

//
// Test the memory pool.
//
TEST(memory, pool) {
    std::vector<int *> ptrs;
    hh::Pool<int> pool;

    pool.fill(10);
    for (size_t i = 0; i < 10; ++i) {
        auto ptr = pool.allocate(false);
        EXPECT_TRUE(ptr != nullptr) << "pool.allocate returned null";
        *ptr = i + 1; // set the ptr to a non 0 value for "no double-alloc" check
        ptrs.push_back(ptr);
    }
    auto ptr = pool.allocate(false);
    EXPECT_TRUE(ptr == nullptr);

    // release the pointers
    for (size_t i = 0; i < 10; ++i) {
        pool.release(ptrs[i]);
    }

    // reallocated them back and make sure they are all present in the array
    for (size_t i = 0; i < 10; ++i) {
        auto ptr = pool.allocate(false);
        EXPECT_TRUE(ptr != nullptr) << "pool.allocate returned null";
        auto it = std::find(ptrs.begin(), ptrs.end(), ptr);
        EXPECT_TRUE(it != ptrs.end());
        EXPECT_TRUE(*ptr != 0) << "double-alloc detected";
        *ptr = 0;
    }
    ptr = pool.allocate(false);
    EXPECT_TRUE(ptr == nullptr);

    // memory should be cleaned up even if the data is not released.
}

struct PipelineTask {
    using inputs = hh::type_list<int, float>;
    using outputs = hh::type_list<int, float>;

    void execute(auto ctx, hh::data_t<int> data) {
        printf("%s::execute<int>(%d)[%ld]{%d, %d}\n", ctx->name().c_str(), *data,
               ctx->thread_index(), ctx->numa_id(), ctx->device_id());
        ASSERT_EQ(ctx->numa_id(), 1);
        ASSERT_EQ(ctx->device_id(), 2);
        ctx->push_result(data);
    }

    void execute(auto ctx, hh::data_t<float> data) {
        printf("%s::execute<float>(%f)[%ld]{%d, %d}\n", ctx->name().c_str(), *data,
               ctx->thread_index(), ctx->numa_id(), ctx->device_id());
        ASSERT_EQ(ctx->numa_id(), 3);
        ASSERT_EQ(ctx->device_id(), 4);
        ctx->push_result(data);
    }
};

struct Pipeline {
    auto make_graph(size_t) {
        auto node1 = hh::make_task<PipelineTask>(2, "task1");
        auto node2 = hh::make_task<PipelineTask>(2, "task2");
        auto graph = hh::make_graph<2, int, float, int, float>();

        graph->connect_inputs(node1);
        graph->draw_edges(node1, node2);
        graph->connect_outputs(node2);
        return graph;
    }

    size_t send_to(hh::data_t<int>) {
        return 0;
    }

    size_t send_to(hh::data_t<float>) {
        return 1;
    }
};

//
// Test the default pipeline API.
//
TEST(pipeline, simple) {
    auto pipeline = hh::make_pipeline<Pipeline>({{1, 2}, {3, 4}});
    auto graph = hh::make_graph<2, int, float, int, float>("PipelineGraph");
    graph->connect_inputs(pipeline);
    graph->connect_outputs(pipeline);

    graph->start();
    graph->push_data(hh::make_data<int>(4));
    graph->push_data(hh::make_data<float>(3.14f));
    auto test_value = [&](auto value) {
        using value_type = decltype(value);
        if constexpr (std::is_same_v<value_type, std::shared_ptr<int>>) {
            ASSERT_EQ(*value, 4) << "int received";
        } else if constexpr (std::is_same_v<value_type, std::shared_ptr<float>>) {
            ASSERT_EQ(*value, 3.14f) << "float received";
        }
    };
    std::visit(test_value, graph->get_result());
    std::visit(test_value, graph->get_result());
    graph->stop();
    graph->generate_dot_file("pipeline.dot");
}

//
// Test the lambda pipeline API.
//
TEST(pipeline, lambda) {
    auto pipeline = hh::make_lambda_pipeline({{1, 2}, {3, 4}}, [](size_t) {
        auto node1 = hh::make_task<PipelineTask>(2, "task1");
        auto node2 = hh::make_task<PipelineTask>(2, "task2");
        auto graph = hh::make_graph<2, int, float, int, float>();

        graph->connect_inputs(node1);
        graph->draw_edges(node1, node2);
        graph->connect_outputs(node2);
        return graph;
    });
    pipeline->set_send_to<int>([](auto) -> size_t {
        return 0;
    });
    pipeline->set_send_to<float>([](auto) -> size_t {
        return 1;
    });
    auto graph = hh::make_graph<2, int, float, int, float>("PipelineGraph");
    graph->connect_inputs(pipeline);
    graph->connect_outputs(pipeline);

    graph->start();
    graph->push_data(hh::make_data<int>(4));
    graph->push_data(hh::make_data<float>(3.14f));
    auto test_value = [&](auto value) {
        using value_type = decltype(value);
        if constexpr (std::is_same_v<value_type, std::shared_ptr<int>>) {
            ASSERT_EQ(*value, 4) << "int received";
        } else if constexpr (std::is_same_v<value_type, std::shared_ptr<float>>) {
            ASSERT_EQ(*value, 3.14f) << "float received";
        }
    };
    std::visit(test_value, graph->get_result());
    std::visit(test_value, graph->get_result());
    graph->stop();
    graph->generate_dot_file("lambda_pipeline.dot");
}

//
// Test that states can be shared between pipelines.
//
TEST(pipeline, state_managers) {
    //
    // Use two different states for the 2 graphs.
    //
    {
        auto state1 = std::make_shared<State>(4);
        auto state2 = std::make_shared<State>(4);
        auto pipeline = hh::make_lambda_pipeline({{1, 2}, {3, 4}}, [state1, state2](size_t index) {
            auto graph = hh::make_graph<2, int, float, int, float>();
            auto sm = hh::make_state_manager(index == 0 ? state1 : state2, "sm");
            graph->connect_inputs(sm);
            graph->connect_outputs(sm);
            return graph;
        });
        pipeline->set_send_to<int>([](auto) -> size_t { return 0; });
        pipeline->set_send_to<float>([](auto) -> size_t { return 1; });
        auto graph = hh::make_graph<2, int, float, int, float>("PipelineGraph");

        graph->connect_inputs(pipeline);
        graph->connect_outputs(pipeline);

        graph->start();
        graph->push_data(hh::make_data<int>(4));
        graph->eat_results();
        EXPECT_EQ(state1->int_count, 1);
        EXPECT_EQ(state1->float_count, 0);
        EXPECT_EQ(state1->counter, 8);
        EXPECT_EQ(state2->int_count, 0);
        EXPECT_EQ(state2->float_count, 0);
        EXPECT_EQ(state2->counter, 4);
        graph->push_data(hh::make_data<float>(0.5f));
        graph->eat_results();
        EXPECT_EQ(state1->int_count, 1);
        EXPECT_EQ(state1->float_count, 0);
        EXPECT_EQ(state1->counter, 8);
        EXPECT_EQ(state2->int_count, 0);
        EXPECT_EQ(state2->float_count, 1);
        EXPECT_EQ(state2->counter, 2);
        graph->stop();
    }

    //
    // Use a single state for the 2 graphs.
    //
    {
        auto state = std::make_shared<State>(4);
        auto pipeline = hh::make_lambda_pipeline({{1, 2}, {3, 4}}, [state](size_t) {
            auto graph = hh::make_graph<2, int, float, int, float>();
            auto sm = hh::make_state_manager(state, "sm");
            graph->connect_inputs(sm);
            graph->connect_outputs(sm);
            return graph;
        });
        pipeline->set_send_to<int>([](auto) -> size_t { return 0; });
        pipeline->set_send_to<float>([](auto) -> size_t { return 1; });
        auto graph = hh::make_graph<2, int, float, int, float>("PipelineGraph");

        graph->connect_inputs(pipeline);
        graph->connect_outputs(pipeline);

        graph->start();
        graph->push_data(hh::make_data<int>(4));
        graph->eat_results();
        EXPECT_EQ(state->int_count, 1);
        EXPECT_EQ(state->float_count, 0);
        EXPECT_EQ(state->counter, 8);
        graph->push_data(hh::make_data<float>(0.5f));
        graph->eat_results();
        EXPECT_EQ(state->int_count, 1);
        EXPECT_EQ(state->float_count, 1);
        EXPECT_EQ(state->counter, 4);
        graph->stop();
    }
}

//
// Test custom edges (basic filtering in this case).
//
TEST(edge, custom_edges) {
    size_t node1_int_count = 0, node1_float_count = 0;
    size_t node2_int_count = 0, node2_float_count = 0;
    size_t edge_node1_int_count = 0, edge_node1_float_count = 0;
    size_t edge_node2_int_count = 0, edge_node2_float_count = 0;

    auto node1 = hh::make_lambda_task<2, int, float, int, float>(1, "node1");
    node1->set_execute<int>([&](auto ctx, auto data) {
        printf("%s::execute<int>(%d)[%ld]\n", ctx->name().c_str(), *data, ctx->thread_index());
        ctx->push_result(data);
        ++node1_int_count;
    });
    node1->set_execute<float>([&](auto ctx, auto data) {
        printf("%s::execute<float>(%f)[%ld]\n", ctx->name().c_str(), *data, ctx->thread_index());
        ctx->push_result(data);
        ++node1_float_count;
    });

    auto node2 = hh::make_lambda_task<2, int, float, int, float>(1, "node2");
    node2->set_execute<int>([&](auto ctx, auto data) {
        printf("%s::execute<int>(%d)[%ld]\n", ctx->name().c_str(), *data, ctx->thread_index());
        ctx->push_result(data);
        ++node2_int_count;
    });
    node2->set_execute<float>([&](auto ctx, auto data) {
        printf("%s::execute<float>(%f)[%ld]\n", ctx->name().c_str(), *data, ctx->thread_index());
        ctx->push_result(data);
        ++node2_float_count;
    });

    auto graph = hh::make_graph<2, int, float, int, float>();

    graph->connect_input<int>(node1, hh::Edge<int>([&](hh::Edge<int> *, hh::data_t<int> data, hh::RuntimeInfo const &info) {
        printf("edge<int>(%s)\n", node1->info().name.c_str());
        node1->push_data(std::move(data), info);
        ++edge_node1_int_count;
    }));
    graph->connect_input<float>(node1, hh::Edge<float>([&](hh::Edge<float> *, hh::data_t<float>, hh::RuntimeInfo const &) {
        printf("edge<float>(%s)\n", node1->info().name.c_str());
        ++edge_node1_float_count;
    }));
    graph->connect_input<int>(node2, hh::Edge<int>([&](hh::Edge<int> *, hh::data_t<int>, hh::RuntimeInfo const &) {
        printf("edge<int>(%s)\n", node2->info().name.c_str());
        ++edge_node2_int_count;
    }));
    graph->connect_input<float>(node2, hh::Edge<float>([&](hh::Edge<float> *, hh::data_t<float> data, hh::RuntimeInfo const &info) {
        printf("edge<float>(%s)\n", node2->info().name.c_str());
        node2->push_data(std::move(data), info);
        ++edge_node2_float_count;
    }));
    graph->connect_outputs(node1);
    graph->connect_outputs(node2);

    graph->start();
    graph->push_data(hh::make_data<int>(0));
    graph->push_data(hh::make_data<float>(0.0f));
    graph->eat_results(2);
    graph->stop();

    ASSERT_EQ(edge_node1_int_count, 1);
    ASSERT_EQ(edge_node1_float_count, 1);
    ASSERT_EQ(edge_node2_int_count, 1);
    ASSERT_EQ(edge_node2_float_count, 1);
    ASSERT_EQ(node1_int_count, 1);
    ASSERT_EQ(node1_float_count, 0);
    ASSERT_EQ(node2_int_count, 0);
    ASSERT_EQ(node2_float_count, 1);
}

