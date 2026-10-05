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

#ifndef HEDGEHOG_IMPL_TASK_TRIGGER_H
#define HEDGEHOG_IMPL_TASK_TRIGGER_H

#include "../../tool/macros.hpp"

//
// Triggers allow thread to wait and be notified.
//

namespace hh {

// cond trigger ////////////////////////////////////////////////////////////////

//
// This implementation uses a condition variable. It is mainly used for
// comparing with the older version of hedgehog that was also using a condition
// variable. For hedgehog-v2, using a semaphore is simpler and more efficient.
//

struct CondTrigger {
    std::mutex mutex{};
    std::condition_variable cond{};
    bool terminated = false;

    void initialize() {
        terminated = false;
    }

    void finalize() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            terminated = true;
        }
        cond.notify_all();
    }

    void signal(SignalOpts const &opts) {
        std::lock_guard<std::mutex> lock(mutex); // lock to avoid lost wakeup
        if (opts.count == 1) [[likely]] {
            cond.notify_one();
        } else {
            cond.notify_all();
        }
    }

    WaitResult wait(auto pred) {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [&]{
            return pred() || terminated;
        });
        return WaitResult{terminated, false};
    }
};

// atomic trigger //////////////////////////////////////////////////////////////

//
// Trigger using C++20 std::atomic::wait/notify. Uses non-accumulating
// signaling: the common single-push path does CAS 0→1 (idempotent), so rapid
// pushes don't inflate a counter and cause spurious wakeups when inputs drain
// in a while loop. Multi-signal (count>1) uses fetch_add for explicit
// multi-thread wake.
//

struct alignas(64) AtomicTrigger {
    size_t number_threads_{0};
    alignas(64) std::atomic<int32_t> flag_{0};
    std::atomic<bool> terminated_{false};

    void initialize(size_t number_threads) {
        number_threads_ = number_threads;
        terminated_.store(false);
        flag_.store(0, std::memory_order_relaxed);
    }

    void finalize() {
        terminated_.store(true, std::memory_order_release);
        flag_.fetch_add(static_cast<int32_t>(number_threads_), std::memory_order_release);
        flag_.notify_all();
    }

    void signal(SignalOpts const &opts) {
        if (opts.count == 1) [[likely]] {
            int32_t expected = 0;
            if (flag_.compare_exchange_strong(expected, 1,
                    std::memory_order_release, std::memory_order_relaxed)) {
                flag_.notify_one();
            }
        } else {
            flag_.fetch_add(static_cast<int32_t>(opts.count), std::memory_order_release);
            flag_.notify_all();
        }
    }

    WaitResult wait([[maybe_unused]] RuntimeInfo const &info) {
        for (;;) {
            auto val = flag_.load(std::memory_order_acquire);
            if (val > 0) {
                if (flag_.compare_exchange_weak(val, val - 1,
                        std::memory_order_acquire, std::memory_order_relaxed))
                    return WaitResult{terminated_.load(std::memory_order_acquire), false};
                continue;
            }
            flag_.wait(0, std::memory_order_acquire);
        }
    }
};

// spin trigger ////////////////////////////////////////////////////////////////

//
// TODO: user space only trigger that spins instead of waiting (use mm_pause
//       and possibly yield)
//

} // end namespace hh

#endif
