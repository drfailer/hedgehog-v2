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

#ifndef HEDGEHOG_TOOL_PROFILING_H
#define HEDGEHOG_TOOL_PROFILING_H

#include <unordered_map>
#include <string>
#include <cstddef>
#include <cmath>
#include <chrono>

#include "macros.hpp"

//
// To simplify things and reduce template madness, there is only a single
// profiler type in Hedgehog (this allows any component to have a profiler
// without the need to share configuration or provide adapters to merge
// information from different profiler types). Unlike the previous version, the
// profiling API is thought to be more flexible and should not only be used
// internally. User tasks can access the profiler to augment the profile
// information.
//

// TODO: add HH_ENABLE_PROFILING in the profiler functions

#ifdef HH_ENABLE_PROFILING
#define HH_PROFILE_REGION(profiler, name) \
    thread_local static auto HH_CONCAT(_profile_, __LINE__) = profiler.create_profile(name); \
    for (bool \
         HH_CONCAT(_prof_, __LINE__) = HH_CONCAT(_profile_, __LINE__)->region.begin(); \
         HH_CONCAT(_prof_, __LINE__); \
         HH_CONCAT(_prof_, __LINE__) = HH_CONCAT(_profile_, __LINE__)->region.end())
#else
#define HH_PROFILE_REGION(profiler, name)
#endif

namespace hh {

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Duration = std::chrono::duration<double, std::nano>; // TODO: de we really want doubles?

//
// The profile stores all the profiler data for a particular id.
//
// To compute an estimate of the mean and the variance without having to store
// all the timers, we use the Welford's algorithm.
// wiki: https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance
//

struct ProfileRegion {
    size_t count;
    double mean;
    double m2;
    double min;
    double max;
    TimePoint t0;

    double stddev() {
        return std::sqrt(this->m2 / this->count);
    }

    bool begin() {
        this->t0 = Clock::now();
        return true;
    }

    bool end() {
        TimePoint t1 = Clock::now();
        Duration duration = t1 - this->t0;
        double dur_count = duration.count();

        // Welford's algorithm to accumulate the mean and the variance
        this->count += 1;
        auto old_mean = this->mean;
        this->mean += (dur_count - this->mean) / this->count;
        this->m2 += (dur_count - old_mean) * (dur_count - this->mean);
        this->min = std::min(this->min, dur_count);
        this->max = std::max(this->max, dur_count);
        return false;
    }
};

//
// Using a union for this might be better, but I don't want to use a variant
// (too slow) and unions don't work with RAII...
//
struct Profile {
    ProfileRegion region; // profiling region
    std::string info;     // add information to the report
};

// TODO: do we want this struct to be empty when profiling is disabled (make the node smaller)?
struct Profiler {
    std::unordered_map<std::string, std::unique_ptr<Profile>> profiles;

    Profiler() = default;
    Profiler(Profiler const &) = delete;

    void initialize() {
        profiles.clear();
    }

    void finalize() {}

    //
    // To reduce the impact of the profiler on the runtime, we try to avoid
    // hash map lookups during the computation. Here is the intended way to use
    // this profiling system:
    //
    // thread_local static auto profile = profiler.create_profile(); // an id has to be created beforehand
    // profile->region.begin();
    // ...
    // profiler->region.end();
    //

    Profile *create_profile(std::string const &name) {
        auto profile = std::make_unique<Profile>();
        auto ptr = profile.get();
        profiles[name] = std::move(profile);
        return ptr;
    }
};

// TODO: this is a basic idea on how we could compile profiling information,
// however, we need to be able to make the difference between
// edges/nodes/graph/pipeline (group entries by level)

struct ProfilerStats {
    size_t count;
    double mean;
    double stddev;
    double min;
    double max;
};

struct ProfilerReport {
    ProfilerStats stats;
    std::unordered_map<std::string, ProfilerReport> entries;

    void add_profiler(Profiler const &profiler) {
        // TODO
    }

    std::string to_dot() {
        return "TODO";
    }

    std::string to_json() {
        return "TODO";
    }

    // TODO: binary format
};

// TODO: merging profile data

} // end namespace hh

#endif
