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

#ifndef HEDGEHOG_TOOL_NUMA_H
#define HEDGEHOG_TOOL_NUMA_H

#include <string>
#include <vector>
#include <sstream>
#include "log.hpp"

#if defined(__linux__)
    #include <sched.h>
    #include <fstream>
    #define HH_NUMA_LINUX
#elif defined(__FreeBSD__)
    #include <pthread.h>
    #include <pthread_np.h>
    #include <sys/cpuset.h>
    #include <fstream>
    #define HH_NUMA_FREEBSD
#elif defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #define HH_NUMA_WINDOWS
#endif

namespace hh::numa {

inline std::vector<int> parse_cpulist(std::string const &cpulist) {
    std::vector<int> cpus;
    std::istringstream stream(cpulist);
    std::string token;

    while (std::getline(stream, token, ',')) {
        auto dash = token.find('-');
        if (dash == std::string::npos) {
            cpus.push_back(std::stoi(token));
        } else {
            int lo = std::stoi(token.substr(0, dash));
            int hi = std::stoi(token.substr(dash + 1));
            for (int i = lo; i <= hi; ++i) {
                cpus.push_back(i);
            }
        }
    }
    return cpus;
}

// Linux: sched_setaffinity + sysfs topology ///////////////////////////////////

#if defined(HH_NUMA_LINUX)

inline void pin_current_thread(int numa_id) {
    if (numa_id < 0) return;

    std::string path = "/sys/devices/system/node/node" + std::to_string(numa_id) + "/cpulist";
    std::ifstream file(path);
    if (!file.is_open()) {
        log::warning(std::source_location::current(), "NUMA node ", numa_id, " not found (", path, ")");
        return;
    }

    std::string cpulist;
    std::getline(file, cpulist);
    auto cpus = parse_cpulist(cpulist);

    if (cpus.empty()) {
        log::warning(std::source_location::current(), "NUMA node ", numa_id, " has no CPUs");
        return;
    }

    cpu_set_t set;
    CPU_ZERO(&set);
    for (int cpu : cpus) {
        CPU_SET(cpu, &set);
    }

    if (sched_setaffinity(0, sizeof(cpu_set_t), &set) != 0) {
        log::warning(std::source_location::current(), "sched_setaffinity failed for NUMA node ", numa_id);
    }
}

// FreeBSD: pthread_setaffinity_np + sysfs-compatible topology /////////////////

#elif defined(HH_NUMA_FREEBSD)

inline void pin_current_thread(int numa_id) {
    if (numa_id < 0) return;

    std::string path = "/sys/devices/system/node/node" + std::to_string(numa_id) + "/cpulist";
    std::ifstream file(path);
    if (!file.is_open()) {
        log::warning(std::source_location::current(), "NUMA node ", numa_id, " not found (", path, ")");
        return;
    }

    std::string cpulist;
    std::getline(file, cpulist);
    auto cpus = parse_cpulist(cpulist);

    if (cpus.empty()) {
        log::warning(std::source_location::current(), "NUMA node ", numa_id, " has no CPUs");
        return;
    }

    cpuset_t set;
    CPU_ZERO(&set);
    for (int cpu : cpus) {
        CPU_SET(cpu, &set);
    }

    if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset_t), &set) != 0) {
        log::warning(std::source_location::current(), "pthread_setaffinity_np failed for NUMA node ", numa_id);
    }
}

// Windows: GetNumaNodeProcessorMaskEx + SetThreadGroupAffinity ////////////////

#elif defined(HH_NUMA_WINDOWS)

inline void pin_current_thread(int numa_id) {
    if (numa_id < 0) return;

    GROUP_AFFINITY affinity = {};
    if (!GetNumaNodeProcessorMaskEx(static_cast<USHORT>(numa_id), &affinity)) {
        log::warning(std::source_location::current(), "NUMA node ", numa_id, " not found (GetNumaNodeProcessorMaskEx failed)");
        return;
    }

    if (!SetThreadGroupAffinity(GetCurrentThread(), &affinity, nullptr)) {
        log::warning(std::source_location::current(), "SetThreadGroupAffinity failed for NUMA node ", numa_id);
    }
}

// Unsupported platform: no-op /////////////////////////////////////////////////

#else

inline void pin_current_thread(int) {}

#endif

} // end namespace hh::numa

#endif
