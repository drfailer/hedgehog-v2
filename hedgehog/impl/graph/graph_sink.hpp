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

#ifndef HEDGEHOG_IMPL_GRAPH_GRAPH_SINK_H
#define HEDGEHOG_IMPL_GRAPH_GRAPH_SINK_H

#include <queue>
#include <variant>
#include <mutex>
#include <semaphore>
#include <cassert>
#include <cstdio>

namespace hh {

//
// Thread safe graph sink (used to collect graph results).
//
template <typename ...Outputs>
struct GraphSink {
    using VariantType = std::variant<data_t<Outputs>...>;
    std::mutex              mutex;
    std::condition_variable cond;
    std::queue<VariantType> results;

    void initialize(InitializationInfo const &) {
        std::lock_guard<std::mutex> lock(mutex);
        while (!results.empty()) results.pop();
    }

    void finalize(InitializationInfo const &) {}

    template <typename T>
    void push_data(data_t<T> data, [[maybe_unused]] RuntimeInfo const &info) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            results.push(std::move(data));
        }
        cond.notify_one();
    }

    VariantType get_result() {
        std::unique_lock<std::mutex> lock(mutex);
        cond.wait(lock, [this]{ return !results.empty(); });
        auto data = std::move(results.front());
        results.pop();
        return data;
    }
};

} // end namespace hh

#endif
