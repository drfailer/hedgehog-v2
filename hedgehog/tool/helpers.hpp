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

#ifndef HEDGEHOG_TOOL_HELPERS_H
#define HEDGEHOG_TOOL_HELPERS_H

#include <cstddef>
#include <new>

namespace hh {

// helper functions ////////////////////////////////////////////////////////////

template <typename Component>
std::shared_ptr<Component> copy_component(std::shared_ptr<Component> component) {
    if constexpr (requires { component->copy(); }) {
        return component->copy();
    }
    return std::make_shared<Component>();
}

template <typename Component>
void initialize_component(Component component, InitializationInfo const &info) {
    if constexpr (requires { component->initialize(info); }) {
        component->initialize(info);
    } else if constexpr (requires { component->initialize(); }) {
        component->initialize();
    }
}

template <typename Component>
void finalize_component(Component component, InitializationInfo const &info) {
    if constexpr (requires { component->finalize(info); }) {
        component->finalize(info);
    } else if constexpr (requires { component->finalize(); }) {
        component->finalize();
    }
}

template<typename T>
constexpr auto type_to_string() {
  std::string_view name, prefix, suffix;
#ifdef __clang__
  name = __PRETTY_FUNCTION__;
  prefix = "auto hh::type_to_string() [T = ";
  suffix = "]";
#elif defined(__GNUC__)
  name = __PRETTY_FUNCTION__;
  prefix = "constexpr auto hh::type_to_string() [with T = ";
  suffix = "]";
#elif defined(_MSC_VER)
  name = __FUNCSIG__;
    prefix = "auto __cdecl type_to_string<";
    suffix = ">(void)";
#endif
  name.remove_prefix(prefix.size());
  name.remove_suffix(suffix.size());

  return std::string(name);
}

} // end namespace hh

#endif
