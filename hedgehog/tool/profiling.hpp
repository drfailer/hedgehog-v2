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
#include <vector>
#include <string>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <sstream>
#include <limits>
#include <algorithm>

#ifdef HH_ENABLE_PROFILING
#include <mutex>
#include <memory>
#endif

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

#ifdef HH_ENABLE_PROFILING
#define HH_PROFILE_REGION(profiler, name) \
    thread_local static auto HH_CONCAT(_profile_, __LINE__) = (profiler).profile((name)); \
    for (bool \
         HH_CONCAT(_prof_, __LINE__) = HH_CONCAT(_profile_, __LINE__)->begin_region(); \
         HH_CONCAT(_prof_, __LINE__); \
         HH_CONCAT(_prof_, __LINE__) = HH_CONCAT(_profile_, __LINE__)->end_region())
#else
#define HH_PROFILE_REGION(profiler, name)
#endif

namespace hh {

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Duration = std::chrono::duration<double, std::nano>; // TODO: do we really want doubles?

//
// The measure computes the mean and the stddev using the Welford's algorithm.
//

struct Measure {
    size_t count = 0;
    double mean = 0;
    double m2 = 0;
    double min = std::numeric_limits<double>::max();
    double max = 0;

    void add_value(double x) {
        this->count += 1;
        auto old_mean = this->mean;
        this->mean += (x - this->mean) / this->count;
        this->m2 += (x - old_mean) * (x - this->mean);
        this->min = std::min(this->min, x);
        this->max = std::max(this->max, x);
    }

    void merge(Measure const &m) {
        if (&m == this || m.count == 0) return;
        size_t count = this->count + m.count;
        double delta = this->mean - m.mean;
        double mean = (this->count * this->mean + m.count * m.mean) / count;
        double m2 = this->m2 + m.m2 + delta * delta * this->count * m.count / count;
        this->count = count;
        this->mean = mean;
        this->m2 = m2;
        this->min = std::min(this->min, m.min);
        this->max = std::max(this->max, m.max);
    }

    double stddev() const {
        return std::sqrt(this->m2 / this->count);
    }
};

//
// Using a union for this might be better, but I don't want to use a variant
// (too slow) and unions don't work with RAII...
//

struct ProfileRegion {
    Measure measure;
    TimePoint t0;
};

struct Profile {
    ProfileRegion region;
    std::string info;

    bool begin_region() {
#ifdef HH_ENABLE_PROFILING
        region.t0 = Clock::now();
#endif
        return true;
    }

    bool end_region() {
#ifdef HH_ENABLE_PROFILING
        TimePoint t1 = Clock::now();
        Duration duration = t1 - this->region.t0;
        region.measure.add_value(duration.count());
#endif
        return false;
    }

    void set_info([[maybe_unused]] std::string const &info_str) {
#ifdef HH_ENABLE_PROFILING
        this->info = info_str;
#endif
    }

    void set_info(auto ...args) {
#ifdef HH_ENABLE_PROFILING
        std::ostringstream oss;
        (oss << ... << args);
        this->info = oss.str();
#endif
    }

    bool has_info() const { return info.size() > 0; }
    bool has_region() const { return region.measure.count > 0; }

    void merge(Profile const &profile) {
        if (profile.has_region()) {
            region.measure.merge(profile.region.measure);
        }
        if (profile.has_info()) {
            info += profile.info;
        }
    }
};

enum class ProfileReportKind {
    Node,
    Edge,
    Graph,
    Pipeline,
};

struct ProfilerReport {
    ProfileReportKind kind = ProfileReportKind::Node;
    ProfilerReport *parent = nullptr;
    std::string label = "";
    uintptr_t id = 0;
    uintptr_t sender_id = 0;
    uintptr_t receiver_id = 0;
    std::unordered_map<std::string, Profile> profiles = {};
    std::vector<ProfilerReport> children = {};

    ProfilerReport() = default;

    ProfilerReport(uintptr_t id, uintptr_t sender_id, uintptr_t receiver_id, std::string const &label)
        : kind(ProfileReportKind::Edge), label(label), id(id), sender_id(sender_id), receiver_id(receiver_id) {}

    void add_report(ProfilerReport report) {
        report.parent = this;
        children.push_back(std::move(report));
    }

    void add_profile(std::string const &label, Profile const &profile) {
        auto it = profiles.find(label);
        if (it == profiles.end()) {
            profiles[label] = profile;
        } else {
            profiles[label].merge(profile);
        }
    }

    void merge_children_profiles() {
        for (auto &child : children) {
            for (auto &[label, profile] : child.profiles) {
                add_profile(label, profile);
            }
        }
    }
};

// TODO: do we want this struct to be empty when profiling is disabled (make the node smaller)?
// - we could return a global dummy profile
struct Profiler {
#ifdef HH_ENABLE_PROFILING
    std::mutex mutex;
    std::unordered_map<std::string, std::unique_ptr<Profile>> profiles;
#endif

    Profiler() = default;
    Profiler(Profiler const &) = delete;

    void initialize() {
#ifdef HH_ENABLE_PROFILING
        profiles.clear();
#endif
    }

    void finalize() {}

    //
    // To reduce the impact of the profiler on the runtime, we try to avoid
    // hash map lookups during the computation. Here is the intended way to use
    // this profiling system:
    //
    // thread_local static Profile *profile = profiler.profile(); // an id has to be created beforehand
    // profile->region.begin();
    // ...
    // profiler->region.end();
    //

    Profile *profile([[maybe_unused]] std::string const &name) {
#ifdef HH_ENABLE_PROFILING
        std::lock_guard<std::mutex> lock(mutex);
        auto profile = std::make_unique<Profile>();
        auto ptr = profile.get();
        profiles[name] = std::move(profile);
        return ptr;
#else
        static Profile dummy;
        return &dummy;
#endif
    }

    ProfilerReport create_report(std::string const &label, ProfileReportKind kind) {
        ProfilerReport report;
        report.label = label;
        report.kind = kind;
#ifdef HH_ENABLE_PROFILING
        for (auto &[label, profile] : profiles) {
            report.profiles[label] = *profile.get();
        }
#endif
        return report;
    }
};

} // end namespace hh

#endif
