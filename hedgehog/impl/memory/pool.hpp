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

#ifndef HEDGEHOG_IMPL_MEMORY_POOL_H
#define HEDGEHOG_IMPL_MEMORY_POOL_H

#include <semaphore>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <new>

#include "index_allocator.hpp"

namespace hh {

template <typename T>
struct Pool {
    std::counting_semaphore<> sem_{0};
    IndexAllocator index_allocator_;
    std::byte *mem_;
    size_t capacity_;

    virtual ~Pool() {
        if (!mem_) return;
        auto elements = reinterpret_cast<T*>(mem_);
        for (size_t i = 0; i < capacity_; ++i) {
            elements[i].~T();
        }
        std::free(mem_);
    }

    void fill(size_t count, auto &&...args) {
        capacity_ = count;
        index_allocator_.init(count);
        size_t len_bytes = capacity_ * sizeof(T);
        mem_ = static_cast<std::byte*>(std::aligned_alloc(alignof(T), len_bytes));
        assert(mem_ && "failed to fill pool");
        for (size_t i = 0; i < count; ++i) {
            new (&mem_[i * sizeof(T)]) T(std::forward<decltype(args)>(args)...);
        }
        sem_.release(count);
    }

    T *allocate(bool wait = false) {
        if (wait) {
            sem_.acquire();
        } else {
            if (!sem_.try_acquire()) {
                return nullptr;
            }
        }
        auto index = index_allocator_.allocate();
        assert(index < capacity_);
        auto mem_index = index * sizeof(T);
        return reinterpret_cast<T *>(&mem_[mem_index]);
    }

    void release(T *data) {
        if (!data) return;

        auto mem_index = reinterpret_cast<uintptr_t>(data) - reinterpret_cast<uintptr_t>(mem_);
        assert(mem_index < (capacity_ * sizeof(T)));

        if constexpr (requires { data->clean_memory(); }) {
            data->clean_memory();
        }
        index_allocator_.release(mem_index / sizeof(T)); // the compiler should translate `/` into a shift
        sem_.release();
    }
};

//
// Wrapper supporting multiple types.
//

template <typename ...Types>
struct MultiPool : Pool<Types>... {
    template <typename T>
    Pool<T> *pool() { return static_cast<Pool<T> *>(this); }

    template <typename T>
    void fill(auto &&...args) { Pool<T>::fill(std::forward<decltype(args)>(args)...); }

    template <typename T>
    T *allocate(bool wait = false) { Pool<T>::allocate(wait); }

    template <typename T>
    void release(T *data) { Pool<T>::release(data); }
};

} // end namespace hh

#endif
