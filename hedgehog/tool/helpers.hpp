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

template <typename Iterator, typename Component>
void create_component_copies(Iterator begin, Iterator end, std::shared_ptr<Component> component) {
    *begin = component;
    begin++;
    for (; begin != end; begin++) {
        *begin = copy_component(component);
    }
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

// Uninitialized ///////////////////////////////////////////////////////////////

//
// Some C++ madness to defer RAII initialization (usefull in node components).
//
// It is equivalent to std::optional, but accessing the memory doesn't check if
// it is initialized. This is only meant to be used internally where we need
// deffered initialization, thus we expect that this class will be used
// properly (e.g. construct has to be called at some point, otherwise it might
// crash, you will debug =D).
//

template <typename T>
struct Uninitialized {
    alignas(T) std::byte mem[sizeof(T)];

    Uninitialized() = default;

    template <typename ...Args>
    void construct(Args &&...args) { new (mem) T(std::forward<Args>(args)...); }

    T *get() { return std::launder(reinterpret_cast<T *>(&mem)); }
    T const *get() const { return std::launder(reinterpret_cast<T *>(&mem)); }

    T &operator*() { return *get(); }
    T const &operator*() const { return *get(); }

    T *operator->() { return get(); }
    T const *operator->() const { return get(); }

    // unconditional destruction: this class is here because we want to avoid
    // the check in std::optional
    ~Uninitialized() { get()->~T(); }
};

} // end namespace hh

#endif
