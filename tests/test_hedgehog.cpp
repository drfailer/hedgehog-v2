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
    // using io = hh::io_types<2, int, float, int, float>;
    using inputs = hh::type_list<int, float>;
    using outputs = hh::type_list<int, float>;

    void execute(auto ctx, hh::data_t<int> data) {
        printf("%s::execute<int>(%d)[%ld]\n", ctx.name().c_str(), *data, ctx.thread_index());
        ctx.push_result(data);
    }

    void execute(auto ctx, hh::data_t<float> data) {
        printf("%s::execute<float>(%f)[%ld]\n", ctx.name().c_str(), *data, ctx.thread_index());
        ctx.push_result(data);
    }
};

TEST(compile_test, compile_test) {
    auto node1 = hh::make_task<Task>(2, "task1");
    auto node2 = hh::make_task<Task>(2, "task2");
    auto graph = hh::make_graph<2, int, float, int, float>();
    // auto graph = hh::make_serial_graph<2, int, float, int, float>();

    printf("running first test\n");

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
}

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
