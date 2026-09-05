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
    thread_local static size_t HH_CONCAT(_prof_id_, __LINE__) = profiler.create_profile(name); \
    for (bool \
         HH_CONCAT(_prof_, __LINE__) = profiler.begin_region(HH_CONCAT(prof_id_, __LINE__)); \
         HH_CONCAT(_prof_, __LINE__); \
         HH_CONCAT(_prof_, __LINE__) = profiler.end_region(HH_CONCAT(prof_id_, __LINE__)))
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

struct Profile {
    size_t count;
    double mean;
    double m2;
    double min;
    double max;
    TimePoint region_begin;
    // TODO: nvtx domain
};

// TODO: do we want this struct to be empty when profiling is disabled (make the node smaller)?
struct Profiler {
    std::unordered_map<std::string, size_t> ids_;
    std::vector<Profile> profiles_;

    static void add_duration(Profile &profile, double dur) {
        // Welford's algorithm to accumulate the mean and the variance
        profile.count += 1;
        auto old_mean = profile.mean;
        profile.mean += (dur - profile.mean) / profile.count;
        profile.m2 += (dur - old_mean) * (dur - profile.mean);
        profile.min = std::min(profile.min, dur);
        profile.max = std::max(profile.max, dur);
    }

    static double compute_variance(Profile const &profile) {
        return profile.m2 / profile.count;
    }


    //
    // To reduce the impact of the profiler on the runtime, we try to avoid
    // hash map lookups during the computation. Here is the intended way to use
    // this profiling system:
    //
    // thread_local static size_t id = profiler.create_profile(); // an id has to be created beforehand
    // profiler.begin_region(id);
    // ...
    // profiler.end_region(id);
    //

    size_t create_profile(std::string const &name) {
        size_t id = profiles_.size();
        ids_[name] = id;
        profiles_.emplace_back();
        return id;
    }

    bool begin_region(size_t id) {
        Profile &profile = profiles_[id];
        profile.region_begin = Clock::now();
        return true;
    }

    bool end_region(size_t id) {
        TimePoint region_end = Clock::now();
        Profile &profile = profiles_[id];
        Duration duration = region_end - profile.region_begin;
        add_duration(profile, duration.count());
        return false;
    }
};

// TODO: merging profile data
// TODO: do output

} // end namespace hh

#endif
