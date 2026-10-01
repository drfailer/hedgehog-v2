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

#ifdef __linux__
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#elif defined(_WIN32)
#include <windows.h>
#endif
#include <climits>
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

// semaphore trigger ///////////////////////////////////////////////////////////

//
// Semaphore based trigger implementation. It uses a single semaphore for all
// the threads of the node which is simple but may not be suitable if there are
// too many threads.
//

struct alignas(64) SemaTrigger {
    size_t number_threads_{0};
    std::counting_semaphore<> sem_{0};
    std::atomic<bool> terminated_{false};

    void initialize(size_t number_threads) {
        number_threads_ = number_threads;
        terminated_.store(false);
    }

    void finalize() {
        terminated_.store(true, std::memory_order_release);
        sem_.release(number_threads_);
    }

    void signal(SignalOpts const &opts) {
        sem_.release(opts.count);
    }

    WaitResult wait([[maybe_unused]] RuntimeInfo const &info) {
        sem_.acquire();
        return WaitResult{terminated_.load(std::memory_order_acquire), false};
    }
};

// futex trigger ///////////////////////////////////////////////////////////////

//
// Trigger with configurable spin count backed by OS wait primitives (Linux
// futex / Windows WaitOnAddress). Unlike std::counting_semaphore (which always
// spins 16 iterations in libstdc++), the spin count defaults to 0 for minimal
// latency in compute-heavy workloads.
//
// Falls back to SemaTrigger on unsupported platforms.
//

#if defined(__linux__) || defined(_WIN32)

template <size_t SpinCount = 0>
struct alignas(64) FutexTrigger {
    static constexpr size_t spin_count = SpinCount;

    size_t number_threads_{0};
    alignas(64) std::atomic<int32_t> counter_{0};
    alignas(64) std::atomic<int32_t> waiters_{0};
    std::atomic<bool> terminated_{false};

    void initialize(size_t number_threads) {
        number_threads_ = number_threads;
        terminated_.store(false);
        counter_.store(0, std::memory_order_relaxed);
        waiters_.store(0, std::memory_order_relaxed);
    }

    void finalize() {
        terminated_.store(true, std::memory_order_release);
        counter_.fetch_add(static_cast<int32_t>(number_threads_), std::memory_order_release);
        platform_wake_all();
    }

    void signal(SignalOpts const &opts) {
        counter_.fetch_add(static_cast<int32_t>(opts.count), std::memory_order_seq_cst);
        if (waiters_.load(std::memory_order_seq_cst) > 0)
            platform_wake(opts.count);
    }

    WaitResult wait([[maybe_unused]] RuntimeInfo const &info) {
        for (;;) {
            auto val = counter_.load(std::memory_order_acquire);
            if (val > 0) {
                if (counter_.compare_exchange_weak(val, val - 1,
                        std::memory_order_acquire, std::memory_order_relaxed))
                    return WaitResult{terminated_.load(std::memory_order_acquire), false};
                continue;
            }

            if constexpr (spin_count > 0) {
                for (size_t i = 0; i < spin_count; ++i) {
                    hh_cross_platform_mm_pause();
                    val = counter_.load(std::memory_order_acquire);
                    if (val > 0) break;
                }
                if (val > 0) continue;
            }

            waiters_.fetch_add(1, std::memory_order_seq_cst);
            val = counter_.load(std::memory_order_seq_cst);
            if (val > 0) {
                waiters_.fetch_sub(1, std::memory_order_relaxed);
                continue;
            }
            platform_wait(0);
            waiters_.fetch_sub(1, std::memory_order_relaxed);
        }
    }

private:
#ifdef __linux__
    void platform_wait(int32_t expected) {
        syscall(SYS_futex, reinterpret_cast<uint32_t*>(&counter_),
                FUTEX_WAIT | FUTEX_PRIVATE_FLAG, expected, nullptr, nullptr, 0);
    }

    void platform_wake(size_t count) {
        syscall(SYS_futex, reinterpret_cast<uint32_t*>(&counter_),
                FUTEX_WAKE | FUTEX_PRIVATE_FLAG, static_cast<int>(count), nullptr, nullptr, 0);
    }

    void platform_wake_all() {
        syscall(SYS_futex, reinterpret_cast<uint32_t*>(&counter_),
                FUTEX_WAKE | FUTEX_PRIVATE_FLAG, INT_MAX, nullptr, nullptr, 0);
    }
#elif defined(_WIN32)
    void platform_wait(int32_t expected) {
        WaitOnAddress(&counter_, &expected, sizeof(expected), INFINITE);
    }

    void platform_wake(size_t count) {
        for (size_t i = 0; i < count; ++i)
            WakeByAddressSingle(&counter_);
    }

    void platform_wake_all() {
        WakeByAddressAll(&counter_);
    }
#endif
};

#else
template <int = 0>
using FutexTrigger = SemaTrigger;
#endif

// spin trigger ////////////////////////////////////////////////////////////////

//
// TODO: user space only trigger that spins instead of waiting (use mm_pause
//       and possibly yield)
//

} // end namespace hh

#endif
