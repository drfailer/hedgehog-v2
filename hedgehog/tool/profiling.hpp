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

#include "helpers.hpp"

#ifdef HH_ENABLE_PROFILING
#include <mutex>
#include <memory>

#ifdef HH_USE_NVTX
#include <nvtx3/nvToolsExt.h>
#endif
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
#define HH_THREAD_PROFILE_REGION(profiler, name) \
    thread_local static auto HH_CONCAT(_profile_, __LINE__) = (profiler).profile_region((name)); \
    for (bool \
         HH_CONCAT(_prof_, __LINE__) = HH_CONCAT(_profile_, __LINE__)->begin_region(); \
         HH_CONCAT(_prof_, __LINE__); \
         HH_CONCAT(_prof_, __LINE__) = HH_CONCAT(_profile_, __LINE__)->end_region())
#define HH_PROFILE_REGION(profiler, name) \
    auto HH_CONCAT(_profile_, __LINE__) = (profiler).profile_region((name)); \
    for (bool \
         HH_CONCAT(_prof_, __LINE__) = HH_CONCAT(_profile_, __LINE__)->begin_region(); \
         HH_CONCAT(_prof_, __LINE__); \
         HH_CONCAT(_prof_, __LINE__) = HH_CONCAT(_profile_, __LINE__)->end_region())
#else
#define HH_THREAD_PROFILE_REGION(profiler, name)
#define HH_PROFILE_REGION(profiler, name)
#endif

namespace hh {

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Duration = std::chrono::duration<double, std::nano>;

// Measure /////////////////////////////////////////////////////////////////////

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

// Profile /////////////////////////////////////////////////////////////////////

//
// Using a union for this might be better, but I don't want to use a variant
// (too slow) and unions don't work with RAII...
//

enum class ProfileKind {
    Region,
    String,
};

struct ProfileRegion {
    Measure measure;
    TimePoint t0;
    #ifdef HH_USE_NVTX
    struct ProfileNVTXRe {
        nvtxDomainHandle_t *domain;
        nvtxEventAttributes_t attr;
        nvtxRangeId_t range_id;
    } nvtx;
    #endif
};

struct ProfileString {
    std::string value;
};

struct Profile {
    ProfileKind   kind;
    ProfileRegion region;
    ProfileString string;

    Profile(ProfileKind kind) : kind(kind) {}

    #ifdef HH_USE_NVTX
    Profile(std::string const &name, nvtxDomainHandle_t *nvtx_domain) : kind(ProfileKind::Region) {
        region.nvtx.domain = nvtx_domain;
        region.nvtx.attr = {};
        region.nvtx.attr.version = NVTX_VERSION;
        region.nvtx.attr.size = NVTX_EVENT_ATTRIB_STRUCT_SIZE;
        region.nvtx.attr.colorType = NVTX_COLOR_ARGB;
        region.nvtx.attr.color = 0xFF72ff68;
        region.nvtx.attr.messageType = NVTX_MESSAGE_TYPE_REGISTERED;
        region.nvtx.attr.message.registered = nvtxDomainRegisterStringA(*nvtx_domain, name.c_str());
    }
    #endif

    bool begin_region() {
        #ifdef HH_ENABLE_PROFILING
        assert(kind == ProfileKind::Region);
        region.t0 = Clock::now();
        #ifdef HH_USE_NVTX
        region.nvtx.range_id = nvtxDomainRangeStartEx(*region.nvtx.domain, &region.nvtx.attr);
        #endif
        #endif
        return true;
    }

    bool end_region() {
        #ifdef HH_ENABLE_PROFILING
        TimePoint t1 = Clock::now();
        Duration duration = t1 - this->region.t0;
        region.measure.add_value(duration.count());
        #ifdef HH_USE_NVTX
        nvtxDomainRangeEnd(*region.nvtx.domain, region.nvtx.range_id);
        #endif
        #endif
        return false;
    }

    void set_string(auto ...args) {
        #ifdef HH_ENABLE_PROFILING
        assert(kind == ProfileKind::String);
        std::ostringstream oss;
        (oss << ... << args);
        this->string.value = oss.str();
        #endif
    }
};

// Profiler ////////////////////////////////////////////////////////////////////

struct Profiler {
    #ifdef HH_ENABLE_PROFILING
    std::mutex mutex;
    std::unordered_map<std::string, std::unique_ptr<Profile>> profiles;
    #ifdef HH_USE_NVTX
    nvtxDomainHandle_t nvtx_domain;
    #endif
    #endif

    void initialize([[maybe_unused]] std::string const &name) {
        #ifdef HH_ENABLE_PROFILING
        profiles.clear();
        #ifdef HH_USE_NVTX
        nvtx_domain = nvtxDomainCreateA(name.c_str());
        #endif
        #endif
    }

    void finalize() {
        #ifdef HH_USE_NVTX
        nvtxDomainDestroy(nvtx_domain);
        #endif
    }

    Profile *profile_region([[maybe_unused]] std::string const &label) {
        #ifdef HH_ENABLE_PROFILING
        std::lock_guard<std::mutex> lock(mutex);
        #ifdef HH_USE_NVTX
        auto profile = std::make_unique<Profile>(label, &nvtx_domain);
        #else
        auto profile = std::make_unique<Profile>(ProfileKind::Region);
        #endif
        auto ptr = profile.get();
        profiles[label] = std::move(profile);
        return ptr;
        #else
        static Profile dummy(ProfileKind::Region);
        return &dummy;
        #endif
    }

    void add_string(std::string const &label, auto &&...args) {
        #ifdef HH_ENABLE_PROFILING
        std::lock_guard<std::mutex> lock(mutex);
        auto profile = std::make_unique<Profile>(ProfileKind::String);
        profile->set_string(std::forward<decltype(args)>(args)...);
        profiles[label] = std::move(profile);
        #endif
    }
};

// Report //////////////////////////////////////////////////////////////////////

struct ProfileEntry {
    std::string label;
    ProfileKind kind;
    struct {
        Measure measure;
    } region;
    struct {
        std::vector<std::string> values;
    } string;
};

using ProfileEntries = std::vector<ProfileEntry>;

enum class ProfileReportKind {
    Node,
    Edge,
    Graph,
    Pipeline,
};

struct ProfileReport {
    ProfileReportKind kind = ProfileReportKind::Node;
    ProfileReport *parent = nullptr;
    std::string label = "";
    uintptr_t id = 0;
    uintptr_t sender_id = 0;
    uintptr_t receiver_id = 0;
    ProfileEntries entries = {};
    std::vector<ProfileReport> children = {};

    static inline uintptr_t edge_counter = 0;

    static ProfileReport edge(void *sender, void *receiver, std::string type) {
        ProfileReport report;
        report.kind = ProfileReportKind::Edge;
        report.label = std::move(type);
        report.id = edge_counter++;
        report.sender_id = reinterpret_cast<uintptr_t>(sender);
        report.receiver_id = reinterpret_cast<uintptr_t>(receiver);
        return report;
    }

    static ProfileReport node(void *node, std::string name) {
        ProfileReport report;
        report.kind = ProfileReportKind::Node;
        report.label = std::move(name);
        report.id = reinterpret_cast<uintptr_t>(node);
        return report;
    }

    static ProfileReport graph(void *graph, void *sink, std::string name) {
        ProfileReport report;
        report.kind = ProfileReportKind::Graph;
        report.label = std::move(name);
        report.id = reinterpret_cast<uintptr_t>(graph);
        report.sender_id = reinterpret_cast<uintptr_t>(graph);
        report.receiver_id = reinterpret_cast<uintptr_t>(sink);
        return report;
    }

    static ProfileReport pipeline(void *pipeline, std::string name) {
        ProfileReport report;
        report.kind = ProfileReportKind::Pipeline;
        report.label = std::move(name);
        report.id = reinterpret_cast<uintptr_t>(pipeline);
        return report;
    }

    void add_report(ProfileReport report) {
        report.parent = this;
        children.push_back(std::move(report));
    }

    void add_entries(std::map<std::string, ProfileEntry> const &entry_map) {
        for (auto const &[label, entry] : entry_map) {
            entries.push_back(entry);
        }
    }

    void add_entry(ProfileEntry entry) {
        entries.push_back(std::move(entry));
    }

    void add_profiles([[maybe_unused]] Profiler const &profiler) {
        #ifdef HH_ENABLE_PROFILING
        for (auto &[label, profile] : profiler.profiles) {
            switch (profile->kind) {
            case ProfileKind::Region:
                entries.push_back(ProfileEntry{
                        label,
                        ProfileKind::Region,
                        profile->region.measure,
                        {},
                });
                break;
            case ProfileKind::String:
                entries.push_back(ProfileEntry{
                        label,
                        ProfileKind::String,
                        {},
                        std::vector<std::string>({profile->string.value}),
                });
                break;
            }
        }
        #endif
    }
};

template <typename T>
std::map<std::string, ProfileEntry> merge_profiles([[maybe_unused]] std::vector<T> const &components) {
    std::map<std::string, ProfileEntry> entry_map;

    #ifdef HH_ENABLE_PROFILING
    for (auto &component : components) {
        auto profiler = &component.profiler;

        for (auto &[label, profile] : profiler->profiles) {
            auto entry_it = entry_map.find(label);
            if (entry_it == entry_map.end()) {
                ProfileEntry new_entry = {label, profile->kind, profile->region.measure, {}};
                if (profile->kind == ProfileKind::String) {
                    new_entry.string.values.push_back(profile->string.value);
                }
                entry_map[label] = new_entry;
            } else {
                if (entry_it->second.kind != profile->kind) {
                    log::fatal("Profile inconsistency found.");
                }
                switch (entry_it->second.kind) {
                case ProfileKind::Region: entry_it->second.region.measure.merge(profile->region.measure); break;
                case ProfileKind::String: entry_it->second.string.values.push_back(profile->string.value); break;
                }
            }
        }
    }
    #endif

    return entry_map;
}

void merge_reports_rec([[maybe_unused]] ProfileReport *dst, [[maybe_unused]] std::vector<ProfileReport> reports) {
    #ifdef HH_ENABLE_PROFILING
    if (reports.empty()) return;

    // the current report is based on reports[0]
    dst->kind        = reports[0].kind;
    dst->label       = reports[0].label;
    dst->id          = reports[0].id;
    dst->sender_id   = reports[0].sender_id;
    dst->receiver_id = reports[0].receiver_id;

    // merge entries
    std::map<std::string, ProfileEntry> entry_map;
    for (auto &report : reports) {
        for (auto &entry : report.entries) {
            auto entry_it = entry_map.find(entry.label);
            if (entry_it == entry_map.end()) {
                entry_map[entry.label] = entry;
            } else {
                switch (entry_it->second.kind) {
                case ProfileKind::Region: entry_it->second.region.measure.merge(entry.region.measure); break;
                case ProfileKind::String:
                    for (auto value : entry.string.values) {
                        entry_it->second.string.values.push_back(value);
                    }
                    break;
                }
            }
        }
    }
    dst->add_entries(entry_map);

    // merge children
    for (size_t child_idx = 0; child_idx < reports[0].children.size(); ++child_idx) {
        ProfileReport child_report;

        // collects all the report versions at child_idx
        std::vector<ProfileReport> child_reports;
        for (auto &report : reports) {
            child_reports.push_back(report.children[child_idx]);
        }

        // merge child report and collect to dst
        merge_reports_rec(&child_report, child_reports);
        dst->add_report(child_report);
    }
    #endif
}

template <typename T>
ProfileReport merge_reports([[maybe_unused]] std::vector<T> components) {
    ProfileReport report;

    #ifdef HH_ENABLE_PROFILING
    if (components.empty()) return report;

    std::vector<ProfileReport> reports;
    for (auto &component : components) {
        reports.push_back(component->profile());
    }
    merge_reports_rec(&report, reports);
    #endif

    return report;
}

} // end namespace hh

#endif
