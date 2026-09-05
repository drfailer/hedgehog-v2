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

#ifndef HEDGEHOG_TOOL_DATA_H
#define HEDGEHOG_TOOL_DATA_H

#include <memory>

//
// The new version of Hedgehog doesn't force users to use shared pointers. One
// can use standard pointers (and use allocators from the library), or raw
// values (we assume that users who want to use falues know what should be
// copied and allocated).
//

namespace hh {

#ifdef HH_VALUE_MODE

// TODO: test this
// In value mode, we allow to use a pointer like API for compatibility with
// other modes (some things like type lists may need to change though).
//

template <typename T>
struct data_t {
    T value;

    data_t() = default;

    template <typename ...Args>
    explicit data_t(Args &&...args) : value(std::forward<Args>(args)...) {}

    data_t(data_t const &data) : value(data.value) {}
    data_t(data_t &&data) : value(std::move(data.value)) {}

    data_t<T> &operator=(data_t const &data) {
        if (&data == this) return *this;
        this->value = data.value;
        return *this;
    }

    data_t<T> &operator=(data_t &&data) {
        this->value = std::move(data.value);
        return *this;
    }

    T &operator*() { return value; }
    T const &operator*() const { return value; }
    T *operator->() { return &value; }
    T const *operator->() const { return &value; }
};

template <typename T>
data_t<T> make_data(auto &&...args) {
    return data_t<T>(std::forward<decltype(args)>(args)...);
}

#elifdef HH_POINTER_MODE

template <typename T>
using data_t = T*;

// TODO: we should have an allocator version when the arena will be implemented

template <typename T>
data_t<T> make_data(auto &&...args) {
    return new T(std::forward<decltype(args)>(args)...);
}

#else

//
// We default to shared pointers like in the previous version (slower but easier
// to use for people who are not confortable with manual memory management).
//

template <typename T>
using data_t = std::shared_ptr<T>;

template <typename T>
data_t<T> make_data(auto &&...args) {
    return std::make_shared<T>(std::forward<decltype(args)>(args)...);
}

#endif

} // end namespace hh

#endif
