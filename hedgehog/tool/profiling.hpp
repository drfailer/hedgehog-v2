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
#include <vector>
#include <string>
#include <variant>
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
#endif

#ifdef HH_USE_NVTX
#include <nvtx3/nvToolsExt.h>
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

// Region profiling macros: timing + NVTX when both are enabled ////////////////

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

// NVTX-only macros: for NVTX ranges without timing ///////////////////////////

#ifdef HH_USE_NVTX
#define HH_THREAD_NVTX_REGION(profiler, name) \
    thread_local static auto HH_CONCAT(_nvtx_, __LINE__) = (profiler).nvtx().make_region((name)); \
    for (bool \
         HH_CONCAT(_nvtxr_, __LINE__) = HH_CONCAT(_nvtx_, __LINE__).begin(); \
         HH_CONCAT(_nvtxr_, __LINE__); \
         HH_CONCAT(_nvtxr_, __LINE__) = HH_CONCAT(_nvtx_, __LINE__).end())
#define HH_NVTX_REGION(profiler, name) \
    auto HH_CONCAT(_nvtx_, __LINE__) = (profiler).nvtx().make_region((name)); \
    for (bool \
         HH_CONCAT(_nvtxr_, __LINE__) = HH_CONCAT(_nvtx_, __LINE__).begin(); \
         HH_CONCAT(_nvtxr_, __LINE__); \
         HH_CONCAT(_nvtxr_, __LINE__) = HH_CONCAT(_nvtx_, __LINE__).end())
#else
#define HH_THREAD_NVTX_REGION(profiler, name)
#define HH_NVTX_REGION(profiler, name)
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

// NVTX ////////////////////////////////////////////////////////////////////////

#ifdef HH_USE_NVTX

struct NvtxRegion {
    nvtxDomainHandle_t *domain;
    nvtxEventAttributes_t attr;
    nvtxRangeId_t range_id;

    bool begin() {
        range_id = nvtxDomainRangeStartEx(*domain, &attr);
        return true;
    }

    bool end() {
        nvtxDomainRangeEnd(*domain, range_id);
        return false;
    }
};

struct NvtxProfiler {
    nvtxDomainHandle_t domain;

    void initialize(std::string const &name) {
        domain = nvtxDomainCreateA(name.c_str());
    }

    void finalize() {
        nvtxDomainDestroy(domain);
    }

    NvtxRegion make_region(std::string const &name) {
        NvtxRegion region;
        region.domain = &domain;
        region.attr = {};
        region.attr.version = NVTX_VERSION;
        region.attr.size = NVTX_EVENT_ATTRIB_STRUCT_SIZE;
        region.attr.colorType = NVTX_COLOR_ARGB;
        region.attr.color = 0xFF72ff68;
        region.attr.messageType = NVTX_MESSAGE_TYPE_REGISTERED;
        region.attr.message.registered = nvtxDomainRegisterStringA(domain, name.c_str());
        return region;
    }
};

#endif

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
};

struct ProfileString {
    std::string value;
};

struct Profile {
    std::string   label;
    ProfileKind   kind;
    ProfileRegion region;
    ProfileString string;
    #ifdef HH_USE_NVTX
    NvtxRegion nvtx_region;
    bool has_nvtx = false;
    #endif

    Profile(std::string label, ProfileKind kind): label(std::move(label)), kind(kind) {}

    bool begin_region() {
        #ifdef HH_ENABLE_PROFILING
        assert(kind == ProfileKind::Region);
        region.t0 = Clock::now();
        #endif
        #ifdef HH_USE_NVTX
        if (has_nvtx) nvtx_region.begin();
        #endif
        return true;
    }

    bool end_region() {
        #ifdef HH_USE_NVTX
        if (has_nvtx) nvtx_region.end();
        #endif
        #ifdef HH_ENABLE_PROFILING
        TimePoint t1 = Clock::now();
        Duration duration = t1 - this->region.t0;
        region.measure.add_value(duration.count());
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
    std::vector<std::unique_ptr<Profile>> profiles;
    #endif
    #ifdef HH_USE_NVTX
    NvtxProfiler nvtx_;
    #endif

    Profiler() = default;
    Profiler(Profiler const &) = delete;
    Profiler([[maybe_unused]] Profiler &&other) {
        #ifdef HH_ENABLE_PROFILING
        std::swap(profiles, other.profiles);
        #endif
        #ifdef HH_USE_NVTX
        std::swap(nvtx_.domain, other.nvtx_.domain);
        #endif
    }

    void initialize([[maybe_unused]] std::string const &name) {
        #ifdef HH_ENABLE_PROFILING
        profiles.clear();
        #endif
        #ifdef HH_USE_NVTX
        nvtx_.initialize(name);
        #endif
    }

    void finalize() {
        #ifdef HH_USE_NVTX
        nvtx_.finalize();
        #endif
    }

    #ifdef HH_USE_NVTX
    NvtxProfiler &nvtx() { return nvtx_; }
    #endif

    Profile *profile_region([[maybe_unused]] std::string label) {
        #ifdef HH_ENABLE_PROFILING
        std::lock_guard<std::mutex> lock(mutex);
        profiles.push_back(std::make_unique<Profile>(std::move(label), ProfileKind::Region));
        auto *p = profiles.back().get();
        #ifdef HH_USE_NVTX
        p->nvtx_region = nvtx_.make_region(p->label);
        p->has_nvtx = true;
        #endif
        return p;
        #else
        static Profile dummy("dummy", ProfileKind::Region);
        return &dummy;
        #endif
    }

    void add_string([[maybe_unused]] std::string label, [[maybe_unused]] auto &&...args) {
        #ifdef HH_ENABLE_PROFILING
        std::lock_guard<std::mutex> lock(mutex);
        profiles.push_back(std::make_unique<Profile>(std::move(label), ProfileKind::String));
        profiles.back()->set_string(std::forward<decltype(args)>(args)...);
        #endif
    }
};

// Report //////////////////////////////////////////////////////////////////////

struct RegionEntry {
    Measure measure;
};

struct StringEntry {
    std::vector<std::string> values;
};

enum class EntryKind { Region, String };

struct ProfileEntry {
    std::string label;
    std::variant<RegionEntry, StringEntry> data;

    EntryKind kind() const { return static_cast<EntryKind>(data.index()); }

    bool is_region() const { return kind() == EntryKind::Region; }
    bool is_string() const { return kind() == EntryKind::String; }

    RegionEntry       &region()       { return std::get<RegionEntry>(data); }
    RegionEntry const &region() const { return std::get<RegionEntry>(data); }
    StringEntry       &string()       { return std::get<StringEntry>(data); }
    StringEntry const &string() const { return std::get<StringEntry>(data); }

    void merge_data(ProfileEntry const &other) {
        std::visit(overloaded{
            [](RegionEntry &dst, RegionEntry const &src) { dst.measure.merge(src.measure); },
            [](StringEntry &dst, StringEntry const &src) {
                for (auto const &v : src.values) dst.values.push_back(v);
            },
            [](auto &, auto const &) { log::fatal("Profile inconsistency found."); },
        }, data, other.data);
    }
};

using ProfileEntries = std::vector<ProfileEntry>;

struct ProfileReport {
    ProfileEntries entries = {};

    void add_entry(ProfileEntry entry) {
        entries.push_back(std::move(entry));
    }

    void add_entries(std::map<std::string, ProfileEntry> const &entry_map) {
        for (auto const &[label, entry] : entry_map) {
            entries.push_back(entry);
        }
    }

    void add_profiles([[maybe_unused]] Profiler const &profiler) {
        #ifdef HH_ENABLE_PROFILING
        for (auto &profile : profiler.profiles) {
            switch (profile->kind) {
            case ProfileKind::Region:
                entries.push_back(ProfileEntry{profile->label, RegionEntry{profile->region.measure}});
                break;
            case ProfileKind::String:
                entries.push_back(ProfileEntry{profile->label, StringEntry{{profile->string.value}}});
                break;
            }
        }
        #endif
    }
};

using ProfileMap = std::map<uintptr_t, ProfileReport>;

// Merge helpers ///////////////////////////////////////////////////////////////

template <typename T>
std::map<std::string, ProfileEntry> merge_profiles([[maybe_unused]] std::vector<T> const &components) {
    std::map<std::string, ProfileEntry> entry_map;

    #ifdef HH_ENABLE_PROFILING
    for (auto &component : components) {
        auto profiler = &component.profiler;

        for (auto &profile : profiler->profiles) {
            ProfileEntry entry = (profile->kind == ProfileKind::Region)
                ? ProfileEntry{profile->label, RegionEntry{profile->region.measure}}
                : ProfileEntry{profile->label, StringEntry{{profile->string.value}}};

            auto entry_it = entry_map.find(profile->label);
            if (entry_it == entry_map.end()) {
                entry_map[profile->label] = std::move(entry);
            } else {
                entry_it->second.merge_data(entry);
            }
        }
    }
    #endif

    return entry_map;
}

} // end namespace hh

#endif
