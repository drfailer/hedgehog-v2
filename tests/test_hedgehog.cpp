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
#include "../hedgehog/hedgehog.h"

// struct TaskV2 : hh::Task<TaskV2> {
//     using inputs  = hh::type_list<int>;
//     using outputs = hh::type_list<int>;
//
//     // we will support both getters (function) and members (static and non static).
//     static constexpr char * name = "Task";
//     const size_t number_threads = 0;
//
//     TaskV2(size_t number_threads) : number_threads(number_threads) {}
//
//     void execute(std::shared_ptr<int> data) {
//         // ...
//         this->add_result(data); // ???
//     }
//
//     auto copy() {
//         return std::make_shared<TaskV2>(this->number_threads);
//     }
// };

static_assert(hh::NodeInputTrait<hh::LockQueueNodeInput<int, float>, int, float>);
static_assert(hh::NodeOutputTrait<hh::DirectNodeOutput<int, float>, int, float>);

struct Task : hh::Task<Task> {
    using inputs = hh::type_list<int, float>;
    using outputs = hh::type_list<int, float>;

    void execute(std::shared_ptr<int>) {}
    void execute(std::shared_ptr<float>) {}
};

TEST(compile_test, compile_test) {
    auto task = std::make_shared<Task>();
    hh::TaskNode<Task> node(task, hh::NodeInfo{});
    ASSERT_EQ(1, 2) << "this should to fail";
}
