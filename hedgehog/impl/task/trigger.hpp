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
    std::function<bool()> pred;

    void initialize(std::function<bool()> fun) {
        this->pred = fun;
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

    WaitResult wait([[maybe_unused]] RuntimeInfo const &info) {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this]{
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

// group trigger ///////////////////////////////////////////////////////////////

//
// TODO: group small amounts of threads on different semaphores. Reduces
//       contention when there are a lot of waiting threads.
//

// spin trigger ////////////////////////////////////////////////////////////////

//
// TODO: user space only trigger that spins instead of waiting (use mm_pause
//       and possibly yield)
//

} // end namespace hh

#endif
