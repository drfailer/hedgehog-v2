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

#ifndef HEDGEHOG_IMPL_TASK_BOUNDED_LOCK_FREE_QUEUE_H
#define HEDGEHOG_IMPL_TASK_BOUNDED_LOCK_FREE_QUEUE_H

#include <atomic>
#include <optional>
#include <unistd.h>
#include <cstdint>

#include "../../tool/macros.hpp"

namespace hh {

// this queue is an implementation of Dmitry Vyukov bounded lock free queue.
template <typename T, size_t Size>
class alignas(64) BoundedLockFreeQueue {
 private:
  struct alignas(64) Slot {
      T data;
      std::atomic<size_t> index;
  };
  static constexpr size_t Mask = Size - 1;
  alignas(64) Slot slots_[Size];
  alignas(64) std::atomic<size_t> head_{0};
  alignas(64) std::atomic<size_t> tail_{0};

 public:
  BoundedLockFreeQueue() {
      static_assert(((Size - 1) & Size) == 0, "Bounded lock free queue Size must be a power of 2.");
      for (size_t i = 0; i < Size; ++i) {
          slots_[i].index = i;
      }
  }

  void push(T data) {
      size_t t = tail_.load();
      size_t full_counter = 0;

      for (;;) {
          size_t index = slots_[t & Mask].index.load(std::memory_order_acquire);
          int64_t diff = static_cast<int64_t>(index) - static_cast<int64_t>(t);

          if (diff == 0) {
              if (tail_.compare_exchange_weak(t, t + 1)) {
                  break;
              }
          } else if (diff < 0) {
              // In this implementation, we consider that there will always be
              // at least one other thread that will try to pop data from the
              // queue. Therefore, we spin in this function until room is freed
              // for the data. Doing so allow to move the data when pushing
              // which is faster when using shared pointers.
              full_counter += 1;
              for (size_t c = 0; c < full_counter; ++c) { hh_cross_platform_yield(); }
          } else {
              hh_cross_platform_yield();
              t = tail_.load();
          }
      }
      slots_[t & Mask].data = std::move(data);
      slots_[t & Mask].index.store(t + 1, std::memory_order_release);
  }

  std::optional<T> pop() {
      size_t h = head_.load();

      for (;;) {
          size_t index = slots_[h & Mask].index.load(std::memory_order_acquire);
          int64_t diff = static_cast<int64_t>(index) - static_cast<int64_t>(h + 1);

          if (diff == 0) {
              if (head_.compare_exchange_weak(h, h + 1, std::memory_order_relaxed)) {
                  break;
              }
          } else if (diff < 0) {
              return std::nullopt;
          } else {
              hh_cross_platform_yield();
              h = head_.load();
          }
      }
      T result = std::move(slots_[h & Mask].data);
      slots_[h & Mask].index.store(h + Size, std::memory_order_release);
      return result;
  }

  size_t size() const {
      return tail_.load(std::memory_order_relaxed) - head_.load(std::memory_order_relaxed);
  }
};

}

#endif
