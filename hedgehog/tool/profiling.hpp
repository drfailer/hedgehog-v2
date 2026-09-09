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

#include <map>
#include <string>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <mutex>
#include <limits>

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

#define HH_ENABLE_PROFILING

#ifdef HH_ENABLE_PROFILING
#define HH_PROFILE_REGION(profiler, name) \
    thread_local static auto HH_CONCAT(_profile_, __LINE__) = (profiler).create_profile((name)); \
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
using Duration = std::chrono::duration<double, std::nano>; // TODO: de we really want doubles?

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

    double stddev() {
        return std::sqrt(this->m2 / this->count);
    }
};

struct ProfileRegion {
    Measure measure;
    TimePoint t0;
};

//
// Using a union for this might be better, but I don't want to use a variant
// (too slow) and unions don't work with RAII...
//
struct Profile {
    ProfileRegion region; // profiling region
    std::string info;     // add information to the report

    bool begin_region() {
        region.t0 = Clock::now();
        return true;
    }

    bool end_region() {
        TimePoint t1 = Clock::now();
        Duration duration = t1 - this->region.t0;
        region.measure.add_value(duration.count());
        return false;
    }

    void set_info(std::string const &info_str) {
        this->info = info_str;
    }

    void set_info(auto ...args) {
        std::ostringstream oss;
        (oss << ... << args);
        this->info = oss.str();
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

// TODO: how to make this easily expandable?
enum class ProfileReportKind {
    Node,
    Edge,
    Graph,
    Pipeline,
};

struct ProfilerReport {
    ProfilerReport *parent = nullptr;
    ProfileReportKind kind;
    std::string label = "";
    std::map<std::string, Profile> profiles = {};
    std::vector<ProfilerReport> childs;

    void add_report(ProfilerReport report) {
        report.parent = this;
        childs.push_back(std::move(report));
    }

    void add_profile(std::string const &label, Profile const &profile) {
        auto it = profiles.find(label);
        if (it == profiles.end()) {
            profiles[label] = profile;
        } else {
            profiles[label].merge(profile);
        }
    }

    void print() {
        switch (kind) {
        case ProfileReportKind::Node: printf("node %s:\n", label.c_str()); break;
        case ProfileReportKind::Edge: printf("edge %s:\n", label.c_str()); break;
        case ProfileReportKind::Graph: printf("graph %s:\n", label.c_str()); break;
        }
        for (auto &[label, profile] : profiles) {
            if (profile.has_region()) {
                printf("- %s: %.3f +- %.3f [%.3f; %.3f] (%ld)\n", label.c_str(),
                       profile.region.measure.mean, profile.region.measure.stddev(),
                       profile.region.measure.min, profile.region.measure.max,
                       profile.region.measure.count);
            }
            if (profile.has_info()) {
                printf("  - %s\n", profile.info.c_str());
            }
        }
        for (auto &child : childs) {
            child.print();
        }
    }

    void to_dot(std::ostream &os, size_t lvl = 0) {
        // TODO
    }
};

// TODO: do we want this struct to be empty when profiling is disabled (make the node smaller)?
// - we could return a global dummy profile
struct Profiler {
    std::mutex mutex;
    std::map<std::string, std::unique_ptr<Profile>> profiles;

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
    // thread_local static Profile *profile = profiler.create_profile(); // an id has to be created beforehand
    // profile->region.begin();
    // ...
    // profiler->region.end();
    //

    Profile *create_profile(std::string const &name) {
        std::lock_guard<std::mutex> lock(mutex);
        auto profile = std::make_unique<Profile>();
        auto ptr = profile.get();
        profiles[name] = std::move(profile);
        return ptr;
    }

    ProfilerReport create_report(std::string const &label, ProfileReportKind kind) {
        ProfilerReport report;
        report.label = label;
        report.kind = kind;
        for (auto &[label, profile] : profiles) {
            report.profiles[label] = *profile.get();
        }
        return report;
    }
};

} // end namespace hh

#endif
