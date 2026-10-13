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

#ifndef HEDGEHOG_IMPL_PROFLING_NVTX_H
#define HEDGEHOG_IMPL_PROFLING_NVTX_H

#define NVTX_NO_IMPL
#ifndef HH_USE_NVTX
#define HH_USE_NVTX
#endif

#include <nvtx3/nvToolsExt.h>

#include <charconv>
#include <chrono>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
#include <unistd.h>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#endif

namespace detail {

// helpers /////////////////////////////////////////////////////////////////////

inline uint64_t readCycles() noexcept {
    #if defined(__x86_64__) || defined(_M_X64)
    return __rdtsc();

    #elif defined(__aarch64__) || defined(_M_ARM64)
    uint64_t val;
    asm volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;

    #elif defined(__powerpc__) || defined(__ppc__) || defined(__PPC__)
    return __builtin_ppc_get_timebase();

    #elif defined(__riscv) && (__riscv_xlen == 64)
    uint64_t val;
    asm volatile("rdtime %0" : "=r"(val));
    return val;

    #endif

    return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

inline double getTscFreqGhz() noexcept {
    static const double tscFreqGhz = []() -> double {
        #if defined(__aarch64__) || defined(_M_ARM64)
        uint64_t freq_hz = 0;
        asm volatile("mrs %0, cntfrq_el0" : "=r"(freq_hz));
        if (freq_hz > 0) {
            return static_cast<double>(freq_hz) / 1'000'000'000.0;
        }
        #endif

        #if defined(__x86_64__) || defined(_M_X64) || defined(__powerpc__) || defined(__ppc__) || defined(__riscv)
        // 10ms active spin against CLOCK_MONOTONIC_RAW to eliminate OS sleep jitter
        auto tsStart = timespec{};
        auto tsCurr  = timespec{};
        clock_gettime(CLOCK_MONOTONIC_RAW, &tsStart);
        const auto tscStart = readCycles();

        constexpr auto targetNs = 10'000'000; // 10 ms
        auto elapsedNs = uint64_t{0};

        do {
            clock_gettime(CLOCK_MONOTONIC_RAW, &tsCurr);
            elapsedNs = static_cast<uint64_t>(tsCurr.tv_sec - tsStart.tv_sec) * 1'000'000'000ULL +
                static_cast<uint64_t>(tsCurr.tv_nsec - tsStart.tv_nsec);
        } while (elapsedNs < targetNs);

        const auto tscEnd = readCycles();
        return static_cast<double>(tscEnd - tscStart) / static_cast<double>(elapsedNs);

        #else
        using Period = std::chrono::steady_clock::period;
        return (static_cast<double>(Period::den) / static_cast<double>(Period::num)) / 1'000'000'000.0;
        #endif
    }();

    return tscFreqGhz;
}

inline double getNanoSecondsPerCycle() noexcept {
    static const double nanoSecondsPerCycle = 1.0 / getTscFreqGhz();
    return nanoSecondsPerCycle;
}

inline uint64_t cyclesToNanoSeconds(const uint64_t cycles) noexcept {
    return static_cast<uint64_t>(static_cast<double>(cycles)*getNanoSecondsPerCycle());
}

template<std::integral Int>
Int parseEnv(const char* envVal, Int defaultVal) noexcept {
    if(!envVal) return defaultVal;

    Int val = defaultVal;
    const auto end = envVal + std::strlen(envVal);
    auto [ptr, ec] = std::from_chars(envVal, end, val, 10);

    return (ec == std::errc{} and ptr == end)? val: defaultVal;
}

inline int getMpiRank() noexcept {
    if(const char* inp = std::getenv("OMPI_COMM_WORLD_RANK"))
        return parseEnv(inp, 0);

    if(const char* inp = std::getenv("PMI_RANK"))
        return parseEnv(inp, 0);

    if(const char* inp = std::getenv("PMIX_RANK"))
        return parseEnv(inp, 0);

    if(const char* inp = std::getenv("SLURM_PROCID"))
        return parseEnv(inp, 0);

    if(const char* inp = std::getenv("ALPS_APP_PE"))
        return parseEnv(inp, 0);

    return 0;
}

// NVTX Global Context /////////////////////////////////////////////////////////

using TimePoint = uint64_t;

struct alignas(8) TraceEvent {
    int32_t   domain    = {};
    int32_t   label     = {};
    uint64_t  payload   = {};
    TimePoint startTime = {};
    TimePoint endTime   = {};
};

class NvtxGlobalContext {
  private:
    std::mutex               mutex_   = {};
    std::vector<std::string> strings_ = {"UNNAMED"};

    std::vector<std::vector<TraceEvent>> traceEventQueues_ = {};

  public:
    ~NvtxGlobalContext() {
        dumpTraceEvents();
    }

    static NvtxGlobalContext& instance() {
        static NvtxGlobalContext context;
        return context;
    }

    [[nodiscard]] nvtxDomainHandle_t nvtxDomainCreateA(const std::string_view &domainName) {
        const auto lg = std::lock_guard(mutex_);
        strings_.emplace_back(domainName);
        return reinterpret_cast<nvtxDomainHandle_t>(strings_.size()-1);
    }

    [[nodiscard]] nvtxStringHandle_t nvtxDomainRegisterStringA(const std::string &&string) {
        const auto lg = std::lock_guard(mutex_);
        strings_.emplace_back(string);
        return reinterpret_cast<nvtxStringHandle_t>(strings_.size()-1);
    }

    void retireQueue(std::vector<TraceEvent> &&queue) {
        if(!queue.empty()) {
            const auto lg = std::lock_guard(mutex_);
            traceEventQueues_.emplace_back(std::move(queue));
        }
    }

  private:
    explicit NvtxGlobalContext() {
        strings_.reserve(256);
    }

    void dumpTraceEvents() const {
        const char* envPath = std::getenv("HH_TRACE_FILE");
        std::string path = envPath? envPath: "trace%r.jsonl";

        const std::string rankStr = std::to_string(getMpiRank());
        for(size_t pos = 0; (pos = path.find("%r", pos)) != std::string::npos; pos += rankStr.length()) {
            path.replace(pos, 2, rankStr);
        }

        const std::string pidStr = std::to_string(getpid());
        for(size_t pos = 0; (pos = path.find("%p", pos)) != std::string::npos; pos += rankStr.length()) {
            path.replace(pos, 2, pidStr);
        }

        uint64_t timePointOffset = readCycles();
        for(const auto &traceEvents: traceEventQueues_) {
            for(const auto &event: traceEvents) {
                if(event.startTime == TimePoint{}) continue;
                timePointOffset = std::min(timePointOffset, event.startTime);
            }
        }

        auto out = std::ofstream(path, std::ios::out|std::ios::trunc);
        for(const auto &traceEvents: traceEventQueues_) {
            for(const auto [domainIdx, labelIdx, payload, startTime, endTime]: traceEvents) {
                if(startTime == TimePoint{}) continue;
                const auto &domain = strings_[static_cast<size_t>(domainIdx)];
                const auto &label  = strings_[static_cast<size_t>(labelIdx)];

                out << "{"
                    << R"("domain":")"    << domain                                           << "\","
                    << R"("label":")"     << label                                            << "\","
                    << R"("start_ns":)"   << cyclesToNanoSeconds(startTime - timePointOffset) << ","
                    << R"("elapsed_ns":)" << cyclesToNanoSeconds(endTime - startTime);

                if(0 < payload)
                    out << R"(, "queue_size": )" << payload;

                out << "}\n";
            }
        }
    }
};

// Trace Event Queue ///////////////////////////////////////////////////////////

class TraceEventQueue {
  private:
    std::vector<TraceEvent> queue_ = {};

  public:
    explicit TraceEventQueue() {
        queue_.reserve(parseEnv(std::getenv("HH_QUEUE_SIZE_PER_THREAD"), size_t{32*1024}));
    }

    ~TraceEventQueue() {
        NvtxGlobalContext::instance().retireQueue(std::move(queue_));
    }

    template<typename ...Args>
    auto emplace_back(Args&& ...args) {
        queue_.emplace_back(args...);
        return queue_.size()-1;
    }

    [[nodiscard]] TraceEvent& operator[](const size_t idx) {
        return queue_[idx];
    }
};

} // namespace detail

inline thread_local auto traceEventQueue = detail::TraceEventQueue{};

// NVTX C API //////////////////////////////////////////////////////////////////

extern "C" {

inline nvtxDomainHandle_t nvtxDomainCreateA(const char* name) {
    return detail::NvtxGlobalContext::instance().nvtxDomainCreateA(name);
}

inline void nvtxDomainDestroy([[maybe_unused]] nvtxDomainHandle_t domain) {}

inline nvtxStringHandle_t nvtxDomainRegisterStringA([[maybe_unused]] nvtxDomainHandle_t domain, const char* string) {
    return detail::NvtxGlobalContext::instance().nvtxDomainRegisterStringA(string);
}

inline nvtxRangeId_t nvtxDomainRangeStartEx(nvtxDomainHandle_t domain, const nvtxEventAttributes_t* eventAttrib) {
    const auto start = detail::readCycles();
    return traceEventQueue.emplace_back(
        static_cast<int32_t>(reinterpret_cast<uintptr_t>(domain)),
        static_cast<int32_t>(reinterpret_cast<uintptr_t>(eventAttrib->message.registered)),
        eventAttrib->payload.ullValue,
        start,
        start
    );
}

inline void nvtxDomainRangeEnd([[maybe_unused]] nvtxDomainHandle_t domain, const nvtxRangeId_t id) {
    const auto end = detail::readCycles();
    traceEventQueue[id].endTime = end;
}

inline void nvtxDomainMarkEx(nvtxDomainHandle_t domain, const nvtxEventAttributes_t* eventAttrib) {
    const auto start = detail::readCycles();
    traceEventQueue.emplace_back(
        static_cast<int32_t>(reinterpret_cast<uintptr_t>(domain)),
        static_cast<int32_t>(reinterpret_cast<uintptr_t>(eventAttrib->message.registered)),
        eventAttrib->payload.ullValue,
        start,
        start
    );
}

} // extern "C"

#endif
