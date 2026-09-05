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

// TODO: this should be added as an executable, but it should be built only when needed.

#include "../hedgehog/hedgehog.h"
#include <type_traits>

using test_list = hh::type_list<int, float>;

// append prepend
static_assert(std::is_same_v<hh::type_list_append<test_list, double>,
                             hh::type_list<int, float, double>>);
static_assert(std::is_same_v<hh::type_list_prepend<test_list, double>,
                             hh::type_list<double, int, float>>);

// contains
static_assert(hh::type_list_contains<test_list, int>);
static_assert(hh::type_list_contains<test_list, float>);
static_assert(!hh::type_list_contains<test_list, double>);

// apply
static_assert(std::is_same_v<hh::type_list_apply<test_list, std::shared_ptr>,
                             hh::type_list<std::shared_ptr<int>, std::shared_ptr<float>>>);
static_assert(std::is_same_v<hh::type_list_apply_ptr<test_list>, hh::type_list<int *, float *>>);

static_assert(hh::NodeInputTrait<hh::LockQueueNodeInput<int, float>, int, float>);
static_assert(hh::NodeOutputTrait<hh::DirectNodeOutput<int, float>, int, float>);

// io types
using test_io_types = hh::io_types<2, char, int, float, double>;
static_assert(std::is_same_v<test_io_types::inputs, hh::type_list<char, int>>);
static_assert(std::is_same_v<test_io_types::outputs, hh::type_list<float, double>>);

int main(int argc, char **argv) {
    return 0;
}
