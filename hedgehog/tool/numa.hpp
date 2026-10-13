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

#include <cassert>
#include <cstdlib>
#include <cstddef>
#include <string>
#include <vector>
#include <sstream>
#include "log.hpp"

#if defined(__linux__)
    #include <sys/mman.h>
    #include <sys/syscall.h>
    #include <unistd.h>
    #include <linux/mempolicy.h>
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

//
// Numa layer used to pin threads and allocate memory on specific numa nodes
// (falls back to no pinning / standard allocation when numa configuration is
// not available).
//

namespace hh::numa {

/******************************************************************************/
/*                               Thread Pinning                               */
/******************************************************************************/

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

/******************************************************************************/
/*                             Memory Allocation.                             */
/******************************************************************************/

// Linux: mmap + mbind ////////////////////////////////////////////////////////

#if defined(__linux__)

inline void *alloc_on_node(size_t len_bytes, int numa_id, size_t &alloc_size_out) {
    assert(numa_id >= 0 && numa_id < 64);

    long page_size = sysconf(_SC_PAGESIZE);
    size_t rounded = (len_bytes + page_size - 1) & ~(static_cast<size_t>(page_size) - 1);
    alloc_size_out = rounded;

    void *ptr = mmap(nullptr, rounded, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        log::warning(std::source_location::current(),
                     "mmap failed for NUMA allocation (", len_bytes, " bytes)");
        return nullptr;
    }

    unsigned long nodemask = 1UL << numa_id;
    unsigned long maxnode = sizeof(unsigned long) * 8 + 1;
    long ret = syscall(__NR_mbind, ptr, rounded, MPOL_BIND, &nodemask, maxnode, 0);
    if (ret != 0) {
        log::warning(std::source_location::current(),
                     "mbind failed for NUMA node ", numa_id);
        munmap(ptr, rounded);
        return nullptr;
    }

    return ptr;
}

inline void free_on_node(void *ptr, size_t alloc_size) {
    if (ptr) munmap(ptr, alloc_size);
}

inline int query_node(void *ptr) {
    int status = -1;
    void *pages[] = { ptr };
    long ret = syscall(__NR_move_pages, 0, 1, pages, nullptr, &status, 0);
    if (ret != 0) return -1;
    return status;
}

// Windows: VirtualAllocExNuma ////////////////////////////////////////////////

#elif defined(_WIN32)

inline void *alloc_on_node(size_t len_bytes, int numa_id, size_t &alloc_size_out) {
    assert(numa_id >= 0);
    alloc_size_out = len_bytes;
    void *ptr = VirtualAllocExNuma(
        GetCurrentProcess(), NULL, len_bytes,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE,
        static_cast<DWORD>(numa_id));
    if (!ptr) {
        log::warning(std::source_location::current(),
                     "VirtualAllocExNuma failed for NUMA node ", numa_id);
    }
    return ptr;
}

inline void free_on_node(void *ptr, [[maybe_unused]] size_t alloc_size) {
    if (ptr) VirtualFree(ptr, 0, MEM_RELEASE);
}

inline int query_node(void *) { return -1; }

// Unsupported platform: fallback /////////////////////////////////////////////

#else

inline void *alloc_on_node(size_t len_bytes, int numa_id, size_t &alloc_size_out) {
    log::warning(std::source_location::current(),
                 "NUMA allocation not supported on this platform, falling back to aligned_alloc");
    alloc_size_out = len_bytes;
    return std::aligned_alloc(alignof(std::max_align_t), len_bytes);
}

inline void free_on_node(void *ptr, [[maybe_unused]] size_t alloc_size) {
    std::free(ptr);
}

inline int query_node(void *) { return -1; }

#endif

} // end namespace hh::numa

#endif
