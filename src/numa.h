/*
  DON, a UCI chess playing engine derived from Stockfish

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef NUMA_H_INCLUDED
#define NUMA_H_INCLUDED

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// Support linux very well, but explicitly do NOT support Android,
// because there is no affected systems, not worth maintaining.
#if defined(__linux__) && !defined(__ANDROID__)
    #if !defined(_GNU_SOURCE)
        #define _GNU_SOURCE
    #endif
    #include <sched.h>
#elif defined(_WIN64)
    #if !defined(NOMINMAX)
        #define NOMINMAX  // Disable min()/max() macros
    #endif
    #if !defined(WIN32_LEAN_AND_MEAN)
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <sdkddkver.h>
    #if defined(_WIN32_WINNT) && _WIN32_WINNT < _WIN32_WINNT_WIN7
        #undef _WIN32_WINNT
    #endif
    #if !defined(_WIN32_WINNT)
        // Force to include needed API prototypes
        #define _WIN32_WINNT _WIN32_WINNT_WIN7  // or _WIN32_WINNT_WIN10
    #endif
    #undef UNICODE
    #include <windows.h>
    #if defined(small)
        #undef small
    #endif
#endif

#include "misc.h"
#include "shm.h"

namespace DON {

using CpuIndex  = std::size_t;
using NumaIndex = std::size_t;

#if defined(_WIN64)
// On Windows each processor group can have up to 64 processors.
// https://learn.microsoft.com/en-us/windows/win32/procthread/processor-groups
inline constexpr std::size_t WIN_PROCESSOR_GROUP_SIZE = 64;

// https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadselectedcpusetmasks
using SetThreadSelectedCpuSetMasks_ = BOOL (*)(HANDLE, PGROUP_AFFINITY, USHORT);
// https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadselectedcpusetmasks
using GetThreadSelectedCpuSetMasks_ = BOOL (*)(HANDLE, PGROUP_AFFINITY, USHORT, PUSHORT);
#endif

inline CpuIndex hardware_concurrency() noexcept {
    CpuIndex concurrency = std::thread::hardware_concurrency();

    // Get all processors across all processor groups on windows, since
    // ::hardware_concurrency() only returns the number of processors in
    // the first group, because only these are available to std::thread.
#if defined(_WIN64)
    concurrency = std::max<CpuIndex>(concurrency, GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
#endif

    return concurrency;
}

inline const CpuIndex SYSTEM_THREADS_NB = std::max<CpuIndex>(hardware_concurrency(), 1);

#if defined(_WIN64)

struct WindowsAffinity final {
    std::optional<std::set<CpuIndex>> get_combined() const {
        if (!oldApi.has_value())
            return newApi;
        if (!newApi.has_value())
            return oldApi;

        std::set<CpuIndex> intersect;
        std::set_intersection(oldApi->begin(), oldApi->end(), newApi->begin(), newApi->end(),
                              std::inserter(intersect, intersect.begin()));
        return intersect;
    }

    // Since Windows 11 and Windows Server 2022 thread affinities can span
    // processor groups and can be set as such by a new WinAPI function. However,
    // may need to force using the old API if detect that the process has
    // affinity set by the old API already and want to override that.
    // Due to the limitations of the old API cannot detect its use reliably.
    // There will be cases where detect not use but it has actually been used and vice versa.

    bool likely_used_old_api() const { return oldApi.has_value() || !oldDeterminate; }

    std::optional<std::set<CpuIndex>> oldApi;
    std::optional<std::set<CpuIndex>> newApi;

    // Also provide diagnostic for when the affinity is set to nullopt
    // whether it was due to being indeterminate. If affinity is indeterminate
    // it is best to assume it is not set at all, so consistent with the meaning
    // of the nullopt affinity.
    bool newDeterminate = true;
    bool oldDeterminate = true;
};

inline std::pair<BOOL, std::vector<USHORT>> get_process_group_affinity() noexcept {
    // GetProcessGroupAffinity requires the arrGroup argument to be
    // aligned to 4 bytes instead of just 2.
    constexpr std::size_t ArrGroupMinAlignment = 4;
    static_assert(ArrGroupMinAlignment >= alignof(USHORT));

    // The function should succeed the second time, but it may fail if the
    // group affinity has changed between GetProcessGroupAffinity calls.
    // In such case consider this a hard error, can't work with unstable affinities anyway.
    USHORT groupCount = 1;
    for (int i = 0; i < 2; ++i)
    {
        auto arrGroup =
          std::make_unique<USHORT[]>(groupCount + (ArrGroupMinAlignment / alignof(USHORT) - 1));

        USHORT* alignedArrGroup = align_ptr_up<ArrGroupMinAlignment>(arrGroup.get());

        BOOL status = GetProcessGroupAffinity(GetCurrentProcess(), &groupCount, alignedArrGroup);

        if (status == 0 && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            break;

        if (status != 0)
            return std::make_pair(status,
                                  std::vector(alignedArrGroup, alignedArrGroup + groupCount));
    }

    return std::make_pair(0, std::vector<USHORT>());
}

// On Windows there are two ways to set affinity, and therefore 2 ways to get it.
// These are not consistent, so have to check both. In some cases it is actually
// not possible to determine affinity. For example when two different threads have
// affinity on different processor groups, set using SetThreadAffinityMask,
// cannot retrieve the actual affinities.
// From documentation on GetProcessAffinityMask:
//     > If the calling process contains threads in multiple groups,
//     > the function returns zero for both affinity masks.
// In such cases just give up and assume have affinity for all processors.
// nullopt means no affinity is set, that is, all processors are allowed
inline WindowsAffinity get_process_affinity() noexcept {

    HMODULE hK32Module = GetModuleHandle(TEXT("kernel32.dll"));

    auto getThreadSelectedCpuSetMasks = GetThreadSelectedCpuSetMasks_(
      (void (*)()) GetProcAddress(hK32Module, "GetThreadSelectedCpuSetMasks"));

    WindowsAffinity winAffinity{};

    BOOL status;

    if (getThreadSelectedCpuSetMasks != nullptr)
    {
        USHORT requiredMaskCount;
        status = getThreadSelectedCpuSetMasks(GetCurrentThread(), nullptr, 0, &requiredMaskCount);

        // Expect ERROR_INSUFFICIENT_BUFFER from GetThreadSelectedCpuSetMasks,
        // but other failure is an actual error.
        if (status == 0 && GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        {
            winAffinity.newDeterminate = false;
        }
        else if (requiredMaskCount > 0)
        {
            // If requiredMaskCount then these affinities were never set, but it's not consistent
            // so GetProcessAffinityMask may still return some affinity.
            auto groupAffinities = std::make_unique<GROUP_AFFINITY[]>(requiredMaskCount);

            status = getThreadSelectedCpuSetMasks(GetCurrentThread(), groupAffinities.get(),
                                                  requiredMaskCount, &requiredMaskCount);

            if (status == 0)
                winAffinity.newDeterminate = false;
            else
            {
                std::set<CpuIndex> cpus;
                for (USHORT i = 0; i < requiredMaskCount; ++i)
                {
                    const std::size_t procGroupIndex = groupAffinities[i].Group;
                    for (std::size_t j = 0; j < WIN_PROCESSOR_GROUP_SIZE; ++j)
                        if (groupAffinities[i].Mask & (KAFFINITY(1) << j))
                            cpus.insert(procGroupIndex * WIN_PROCESSOR_GROUP_SIZE + j);
                }

                winAffinity.newApi = std::move(cpus);
            }
        }
    }

    // NOTE: There is no way to determine full affinity using the old API
    //        if individual threads set affinity on different processor groups.

    DWORD_PTR proc, sys;
    status = GetProcessAffinityMask(GetCurrentProcess(), &proc, &sys);

    // If proc == 0 then cannot determine affinity because it spans processor groups.
    // On Windows 11 and Server 2022 it will instead
    //     > If, however, hHandle specifies a handle to the current process, the function
    //     > always uses the calling thread's primary group (which by default is the same
    //     > as the process' primary group) in order to set the
    //     > lpProcessAffinityMask and lpSystemAffinityMask.
    // So it will never be indeterminate here. Can only make assumptions later.
    if (status == 0 || proc == 0)
    {
        winAffinity.oldDeterminate = false;
        return winAffinity;
    }

    // If SetProcessAffinityMask was never called the affinity must span
    // all processor groups, but if it was called it must only span one.

    std::vector<USHORT> groupAffinity;  // Need to capture this later and capturing
                                        // from structured bindings requires c++20.

    std::tie(status, groupAffinity) = get_process_group_affinity();
    if (status == 0)
    {
        winAffinity.oldDeterminate = false;
        return winAffinity;
    }

    if (groupAffinity.size() == 1)
    {
        // Detect the case when affinity is set to all processors and correctly
        // leave affinity.oldApi as nullopt.
        if (GetActiveProcessorGroupCount() != 1 || proc != sys)
        {
            std::set<CpuIndex> cpus;
            std::size_t        procGroupIndex = groupAffinity[0];

            std::uint64_t mask = proc;
            for (std::size_t j = 0; j < WIN_PROCESSOR_GROUP_SIZE; ++j)
                if (mask & (KAFFINITY(1) << j))
                    cpus.insert(procGroupIndex * WIN_PROCESSOR_GROUP_SIZE + j);

            winAffinity.oldApi = std::move(cpus);
        }
    }
    else
    {
        // If got here it means that either SetProcessAffinityMask was never set
        // or on Windows 11/Server 2022.

        // Since Windows 11 and Windows Server 2022 the behavior of
        // GetProcessAffinityMask changed:
        //     > If, however, hHandle specifies a handle to the current process,
        //     > the function always uses the calling thread's primary group
        //     > (which by default is the same as the process' primary group)
        //     > in order to set the lpProcessAffinityMask and lpSystemAffinityMask.
        // In which case can actually retrieve the full affinity.

        if (getThreadSelectedCpuSetMasks != nullptr)
        {
            std::thread th([&]() {
                std::set<CpuIndex> cpus;

                bool affinityFull = true;

                for (auto procGroupIndex : groupAffinity)
                {
                    int activeProcessorCount =
                      GetActiveProcessorCount(static_cast<WORD>(procGroupIndex));

                    // Have to schedule to 2 different processors and the affinities.
                    // Otherwise processor choice could influence the resulting affinity.
                    // Assume the processor IDs within the group are filled sequentially from 0.
                    std::uint64_t combinedProc = std::numeric_limits<std::uint64_t>::max();
                    std::uint64_t combinedSys  = std::numeric_limits<std::uint64_t>::max();

                    for (int i = 0; i < std::min(activeProcessorCount, 2); ++i)
                    {
                        GROUP_AFFINITY grpAffinity;
                        std::memset(&grpAffinity, 0, sizeof(grpAffinity));
                        grpAffinity.Group = static_cast<WORD>(procGroupIndex);
                        grpAffinity.Mask  = KAFFINITY(1) << i;

                        status = SetThreadGroupAffinity(GetCurrentThread(), &grpAffinity, nullptr);
                        if (status == 0)
                        {
                            winAffinity.oldDeterminate = false;
                            return;
                        }

                        SwitchToThread();

                        DWORD_PTR proc2, sys2;
                        status = GetProcessAffinityMask(GetCurrentProcess(), &proc2, &sys2);
                        if (status == 0)
                        {
                            winAffinity.oldDeterminate = false;
                            return;
                        }

                        combinedProc &= proc2;
                        combinedSys &= sys2;
                    }

                    if (combinedProc != combinedSys)
                        affinityFull = false;

                    for (std::size_t j = 0; j < WIN_PROCESSOR_GROUP_SIZE; ++j)
                        if (combinedProc & (KAFFINITY(1) << j))
                            cpus.insert(procGroupIndex * WIN_PROCESSOR_GROUP_SIZE + j);
                }

                // Have to detect the case where the affinity was not set,
                // or is set to all processors so that correctly produce as
                // std::nullopt result.
                if (!affinityFull)
                    winAffinity.oldApi = std::move(cpus);
            });

            th.join();
        }
    }

    return winAffinity;
}

#endif

#if defined(__linux__) && !defined(__ANDROID__)

inline std::set<CpuIndex> get_process_affinity() noexcept {

    std::set<CpuIndex> cpus;

    // For unsupported systems, or in case of a soft error,
    // may assume all processors are available for use.
    [[maybe_unused]] auto set_to_all_cpus = [&]() {
        for (CpuIndex cpuIdx = 0; cpuIdx < SYSTEM_THREADS_NB; ++cpuIdx)
            cpus.insert(cpuIdx);
    };

    // cpu_set_t by default holds 1024 entries. This may not be enough soon,
    // but there is no easy way to determine how many threads there actually is.
    // In this case just choose a reasonable upper bound.
    constexpr CpuIndex MaxCpusCount = 64 * 1024;

    cpu_set_t* mask = CPU_ALLOC(MaxCpusCount);
    if (mask == nullptr)
        std::exit(EXIT_FAILURE);

    constexpr std::size_t MaskSize = CPU_ALLOC_SIZE(MaxCpusCount);

    CPU_ZERO_S(MaskSize, mask);

    int status = sched_getaffinity(0, MaskSize, mask);

    if (status != 0)
    {
        CPU_FREE(mask);
        std::exit(EXIT_FAILURE);
    }

    for (CpuIndex cpuIdx = 0; cpuIdx < MaxCpusCount; ++cpuIdx)
        if (CPU_ISSET_S(cpuIdx, MaskSize, mask))
            cpus.insert(cpuIdx);

    CPU_FREE(mask);

    return cpus;
}

#endif

#if defined(__linux__) && !defined(__ANDROID__)

inline static const auto STARTUP_PROCESSOR_AFFINITY = get_process_affinity();

#elif defined(_WIN64)

inline static const auto STARTUP_PROCESSOR_AFFINITY = get_process_affinity();
inline static const auto STARTUP_OLD_AFFINITY_API_USE =
  STARTUP_PROCESSOR_AFFINITY.likely_used_old_api();

#endif

// Want to abstract the purpose of storing the numa node index somewhat.
// Whoever is using this does not need to know the specifics of the replication
// machinery to be able to access NUMA replicated memory.
class NumaReplicatedAccessToken final {
   public:
    NumaReplicatedAccessToken() noexcept :
        NumaReplicatedAccessToken(0) {}

    explicit NumaReplicatedAccessToken(NumaIndex numaId) noexcept :
        numaIdx(numaId) {}

    NumaIndex numa_index() const noexcept { return numaIdx; }

   private:
    NumaIndex numaIdx;
};

// Designed as immutable, because there is no good reason to alter an already
// existing config in a way that doesn't require recreating it completely, and
// it would be complex and expensive to maintain class invariants.
// The CPU (processor) numbers always correspond to the actual numbering used
// by the system. The NUMA node numbers MAY NOT correspond to the system's
// numbering of the NUMA nodes. In particular, empty nodes may be removed, or
// the user may create custom nodes. It is guaranteed that NUMA nodes are NOT
// empty: every node exposed by NumaConfig has at least one processor assigned.
//
// Use startup affinities so as not to modify its own behavior in time.
//
// Since DON doesn't support exceptions all places where an exception
// should be thrown are replaced by std::exit.
class NumaConfig final {
   public:
    // This function queries the system for the mapping of processors to NUMA nodes.
    // On Linux read from standardized kernel sysfs, with a fallback to single NUMA node.
    // On Windows utilize GetNumaProcessorNodeEx, which has its quirks,
    // see comment for Windows implementation of get_process_affinity()
    static NumaConfig from_system([[maybe_unused]] bool processAffinityRespect = true) noexcept {
        NumaConfig numaCfg = empty();

#if defined(__linux__) && !defined(__ANDROID__)

        std::set<CpuIndex> allowedCpus;

        if (processAffinityRespect)
            allowedCpus = STARTUP_PROCESSOR_AFFINITY;

        auto is_cpu_allowed = [processAffinityRespect, &allowedCpus](CpuIndex cpuIdx) {
            return !processAffinityRespect || allowedCpus.count(cpuIdx) == 1;
        };

        // On Linux things are straightforward, since there's no processor groups and
        // any thread can be scheduled on all processors.
        // Try to gather this information from the sysfs first
        // https://www.kernel.org/doc/Documentation/ABI/stable/sysfs-devices-node

        bool fallbackUse = false;
        auto fallback    = [&]() {
            fallbackUse = true;
            numaCfg     = empty();
        };

        // /sys/devices/system/node/online contains information about active NUMA nodes
        auto nodeIdxStr = read_file_to_string("/sys/devices/system/node/online");
        if (!nodeIdxStr.has_value() || nodeIdxStr->empty())
        {
            fallback();
        }
        else
        {
            *nodeIdxStr = remove_whitespace(*nodeIdxStr);
            for (CpuIndex n : shortened_string_to_indices(*nodeIdxStr))
            {
                // /sys/devices/system/node/node.../cpulist
                std::string path =
                  std::string("/sys/devices/system/node/node") + std::to_string(n) + "/cpulist";
                auto cpuIdxStr = read_file_to_string(path);
                // Now, only bail if the file does not exist. Some nodes may be
                // empty, that's fine. An empty node still has a file that appears
                // to have some whitespace, so need to handle that.
                if (!cpuIdxStr.has_value())
                {
                    fallback();
                    break;
                }
                else
                {
                    *cpuIdxStr = remove_whitespace(*cpuIdxStr);
                    for (CpuIndex cpuIdx : shortened_string_to_indices(*cpuIdxStr))
                        if (is_cpu_allowed(cpuIdx))
                            numaCfg.add_cpu_to_node(n, cpuIdx);
                }
            }
        }

        if (fallbackUse)
            for (CpuIndex cpuIdx = 0; cpuIdx < SYSTEM_THREADS_NB; ++cpuIdx)
                if (is_cpu_allowed(cpuIdx))
                    numaCfg.add_cpu_to_node(NumaIndex{0}, cpuIdx);

#elif defined(_WIN64)

        std::optional<std::set<CpuIndex>> allowedCpus;

        if (processAffinityRespect)
            allowedCpus = STARTUP_PROCESSOR_AFFINITY.get_combined();

        // The affinity cannot be determined in all cases on Windows,
        // but at least guarantee that the number of allowed processors
        // is >= number of processors in the affinity mask. In case the user
        // is not satisfied they must set the processor numbers explicitly.
        auto is_cpu_allowed = [&allowedCpus](CpuIndex cpuIdx) {
            return !allowedCpus.has_value() || allowedCpus->count(cpuIdx) == 1;
        };

        WORD procGroupCount = GetActiveProcessorGroupCount();
        for (WORD procGroup = 0; procGroup < procGroupCount; ++procGroup)
            for (BYTE number = 0; number < WIN_PROCESSOR_GROUP_SIZE; ++number)
            {
                PROCESSOR_NUMBER procNumber;
                procNumber.Group    = procGroup;
                procNumber.Number   = number;
                procNumber.Reserved = 0;

                USHORT nodeNumber;

                BOOL status = GetNumaProcessorNodeEx(&procNumber, &nodeNumber);
                if (status != 0 && nodeNumber != std::numeric_limits<USHORT>::max())
                {
                    CpuIndex cpuIdx = procGroup * WIN_PROCESSOR_GROUP_SIZE + number;
                    if (is_cpu_allowed(cpuIdx))
                        numaCfg.add_cpu_to_node(nodeNumber, cpuIdx);
                }
            }

        // Split the NUMA nodes to be contained within a group if necessary.
        // This is needed between Windows 10 Build 20348 and Windows 11, because
        // the new NUMA allocation behavior was introduced while there was
        // still no way to set thread affinity spanning multiple processor groups.
        // See https://learn.microsoft.com/en-us/windows/win32/procthread/numa-support
        // Also do this is if need to force old API for some reason.
        //
        // Later it appears that needed to actually always force this behavior.
        // While Windows allows this to work now, such assignments have bad interaction
        // with the scheduler - in particular it still prefers scheduling on the thread's
        // "primary" node, even if it means scheduling SMT processors first.
        // See https://learn.microsoft.com/en-us/windows/win32/procthread/processor-groups
        //
        //     Each process is assigned a primary group at creation, and by default all
        //     of its threads' primary group is the same. Each thread's ideal processor
        //     is in the thread's primary group, so threads will preferentially be
        //     scheduled to processors on their primary group, but they are able to
        //     be scheduled to processors on any other group.
        //
        // used to be guarded by if (STARTUP_OLD_AFFINITY_API_USE)
        {
            NumaConfig splitNumaCfg = empty();

            NumaIndex splitNumaIdx = 0;
            for (const auto& cpus : numaCfg.nodes)
            {
                if (cpus.empty())
                    continue;

                std::size_t lstProcGroupIndex = *(cpus.begin()) / WIN_PROCESSOR_GROUP_SIZE;
                for (CpuIndex cpuIdx : cpus)
                {
                    std::size_t procGroupIndex = cpuIdx / WIN_PROCESSOR_GROUP_SIZE;
                    if (lstProcGroupIndex != procGroupIndex)
                    {
                        lstProcGroupIndex = procGroupIndex;
                        ++splitNumaIdx;
                    }
                    splitNumaCfg.add_cpu_to_node(splitNumaIdx, cpuIdx);
                }
                ++splitNumaIdx;
            }

            numaCfg = std::move(splitNumaCfg);
        }

#else

        // Fallback for unsupported systems.
        for (CpuIndex cpuIdx = 0; cpuIdx < SYSTEM_THREADS_NB; ++cpuIdx)
            numaCfg.add_cpu_to_node(NumaIndex{0}, cpuIdx);

#endif

        // Have to ensure no empty NUMA nodes persist.
        numaCfg.remove_empty_numa_nodes();

        // If the user explicitly opts out from respecting the current process affinity
        // then it may be inconsistent with the current affinity (obviously),
        // so consider it custom.
        if (!processAffinityRespect)
            numaCfg.affinityCustom = true;

        return numaCfg;
    }

    // ':'-separated numa nodes
    // ','-separated cpu indices
    // supports "first-last" range syntax for cpu indices
    // For example "0-15,128-143:16-31,144-159:32-47,160-175:48-63,176-191"
    static NumaConfig from_string(std::string_view str) noexcept {
        NumaConfig numaCfg = empty();

        NumaIndex numaIdx = 0;
        for (auto&& nodeStr : split(str, ":"))
        {
            auto indices = shortened_string_to_indices(nodeStr);
            if (!indices.empty())
            {
                for (auto cpuIdx : indices)
                    if (!numaCfg.add_cpu_to_node(numaIdx, cpuIdx))
                        std::exit(EXIT_FAILURE);

                ++numaIdx;
            }
        }

        numaCfg.affinityCustom = true;

        return numaCfg;
    }

    NumaConfig(CpuIndex maxCpuIdx, bool affinityCtm) noexcept :
        maxCpuIndex(maxCpuIdx),
        affinityCustom(affinityCtm) {}

    NumaConfig() noexcept :
        NumaConfig(0, false) {
        auto numCpus = SYSTEM_THREADS_NB;
        add_cpu_range_to_node(NumaIndex{0}, CpuIndex{0}, numCpus - 1);
    }

    NumaConfig(const NumaConfig&) noexcept            = delete;
    NumaConfig(NumaConfig&&) noexcept                 = default;
    NumaConfig& operator=(const NumaConfig&) noexcept = delete;
    NumaConfig& operator=(NumaConfig&&) noexcept      = default;

    bool is_cpu_assigned(CpuIndex cpuIdx) const noexcept { return nodeByCpu.count(cpuIdx) == 1; }

    NumaIndex nodes_size() const noexcept { return nodes.size(); }

    CpuIndex node_cpus_size(NumaIndex numaIdx) const noexcept {
        assert(numaIdx < nodes_size());
        return nodes[numaIdx].size();
    }

    CpuIndex cpus_size() const noexcept { return nodeByCpu.size(); }

    bool requires_memory_replication() const noexcept { return affinityCustom || nodes_size() > 1; }

    std::string to_string() const noexcept {
        std::ostringstream oss;

        for (auto nodeItr = nodes.begin(); nodeItr != nodes.end(); ++nodeItr)
        {
            if (nodeItr != nodes.begin())
                oss << ':';

            auto rangeItr = nodeItr->begin();
            for (auto itr = nodeItr->begin(); itr != nodeItr->end(); ++itr)
            {
                auto nextItr = std::next(itr);
                if (nextItr == nodeItr->end() || *nextItr != *itr + 1)
                {
                    if (rangeItr != nodeItr->begin())
                        oss << ',';

                    if (itr != rangeItr)
                        oss << *rangeItr << '-';

                    oss << *itr;

                    rangeItr = nextItr;
                }
            }
        }

        return oss.str();
    }

    bool suggests_binding_threads(std::size_t threadCount) const noexcept {
        // If can reasonably determine that the threads can't be contained
        // by the OS within the first NUMA node then advise distributing
        // and binding threads. When the threads are not bound can only use
        // NUMA memory replicated objects from the first node, so when the OS
        // has to schedule on other nodes lose performance. Also suggest binding
        // if there's enough threads to distribute among nodes with minimal disparity.
        // Try to ignore small nodes, in particular the empty ones.

        // If the affinity set by the user does not match the affinity given by the OS
        // then binding is necessary to ensure the threads are running on correct processors.
        if (affinityCustom)
            return true;

        // Obviously cannot distribute a single thread, so a single thread should never be bound.
        if (threadCount <= 1)
            return false;

        std::size_t maxNodeSize = 0;
        for (auto&& cpus : nodes)
            if (maxNodeSize < cpus.size())
                maxNodeSize = cpus.size();

        auto is_node_small = [maxNodeSize](const std::set<CpuIndex>& node) {
            return double(node.size()) / maxNodeSize <= 0.6;
        };

        std::size_t notSmallNodeCount = 0;
        for (auto&& cpus : nodes)
            if (!is_node_small(cpus))
                ++notSmallNodeCount;

        return nodes_size() > 1
            && (threadCount > maxNodeSize / 2 || threadCount >= 4 * notSmallNodeCount);
    }

    std::vector<NumaIndex>
    distribute_threads_among_numa_nodes(std::size_t threadCount) const noexcept {
        std::vector<NumaIndex> ns;

        if (nodes_size() == 1)
        {
            // Special case for when there's no NUMA nodes
            // Doesn't buy much, but let's keep the default path simple
            ns.resize(threadCount, NumaIndex{0});
        }
        else
        {
            std::vector<std::size_t> occupation(nodes_size(), 0);
            for (std::size_t threadId = 0; threadId < threadCount; ++threadId)
            {
                NumaIndex bestNumaIdx = 0;

                auto minFill = std::numeric_limits<double>::max();
                for (NumaIndex numaIdx = 0; numaIdx < nodes_size(); ++numaIdx)
                {
                    auto fill = double(1 + occupation[numaIdx]) / node_cpus_size(numaIdx);
                    // NOTE: Do want to perhaps fill the first available node up to 50% first before considering other nodes?
                    //       Probably not, because it would interfere with running multiple instances.
                    //       Basically shouldn't favor any particular node.
                    if (minFill > fill)
                    {
                        minFill     = fill;
                        bestNumaIdx = numaIdx;
                    }
                }

                ns.emplace_back(bestNumaIdx);
                ++occupation[bestNumaIdx];
            }
        }

        return ns;
    }

    NumaReplicatedAccessToken bind_current_thread_to_numa_node(NumaIndex numaIdx) const noexcept {
        if (numaIdx >= nodes_size() || node_cpus_size(numaIdx) == 0)
            std::exit(EXIT_FAILURE);

#if defined(__linux__) && !defined(__ANDROID__)

        cpu_set_t* mask = CPU_ALLOC(maxCpuIndex + 1);
        if (mask == nullptr)
            std::exit(EXIT_FAILURE);

        const std::size_t maskSize = CPU_ALLOC_SIZE(maxCpuIndex + 1);

        CPU_ZERO_S(maskSize, mask);

        for (CpuIndex cpuIdx : nodes[numaIdx])
            CPU_SET_S(cpuIdx, maskSize, mask);

        int status = sched_setaffinity(0, maskSize, mask);

        CPU_FREE(mask);

        if (status != 0)
            std::exit(EXIT_FAILURE);

        // Yield this thread just to be sure it gets rescheduled.
        // This is defensive, allowed because this code is not performance critical.
        sched_yield();

#elif defined(_WIN64)

        // Requires Windows 11. No good way to set thread affinity spanning processor groups before that.
        HMODULE hK32Module = GetModuleHandle(TEXT("kernel32.dll"));

        auto setThreadSelectedCpuSetMasks = SetThreadSelectedCpuSetMasks_(
          (void (*)()) GetProcAddress(hK32Module, "SetThreadSelectedCpuSetMasks"));

        HANDLE threadHandle;

        BOOL status;

        // ALWAYS set affinity with the new API if available,
        // because there's no downsides, and forcibly keep it consistent with
        // the old API should need to use it. i.e. always keep this as a superset
        // of what set with SetThreadGroupAffinity.
        if (setThreadSelectedCpuSetMasks != nullptr)
        {
            // Only available on Windows 11 and Windows Server 2022 onwards.
            auto procGroupCount = static_cast<WORD>(
              ((maxCpuIndex + 1) + WIN_PROCESSOR_GROUP_SIZE - 1) / WIN_PROCESSOR_GROUP_SIZE);
            auto groupAffinities = std::make_unique<GROUP_AFFINITY[]>(procGroupCount);
            std::memset(groupAffinities.get(), 0, procGroupCount * sizeof(*groupAffinities.get()));
            for (WORD procGroup = 0; procGroup < procGroupCount; ++procGroup)
                groupAffinities[procGroup].Group = procGroup;

            for (CpuIndex cpuIdx : nodes[numaIdx])
            {
                std::size_t procGroupIndex   = cpuIdx / WIN_PROCESSOR_GROUP_SIZE;
                std::size_t inProcGroupIndex = cpuIdx % WIN_PROCESSOR_GROUP_SIZE;
                groupAffinities[procGroupIndex].Mask |= KAFFINITY(1) << inProcGroupIndex;
            }

            threadHandle = GetCurrentThread();

            status =
              setThreadSelectedCpuSetMasks(threadHandle, groupAffinities.get(), procGroupCount);
            if (status == 0)
                std::exit(EXIT_FAILURE);

            // Yield this thread just to be sure it gets rescheduled.
            // This is defensive, allowed because this code is not performance critical.
            SwitchToThread();
        }

        // Sometimes need to force the old API, but do not use it unless necessary.
        if (setThreadSelectedCpuSetMasks == nullptr || STARTUP_OLD_AFFINITY_API_USE)
        {
            // On earlier windows version (since windows 7)
            // cannot run a single thread on multiple processor groups, so need to restrict the group.
            // Assume the group of the first processor listed for this node.
            // Processors from outside this group will not be assigned for this thread.
            // Normally this won't be an issue because windows used to assign NUMA nodes such that
            // they cannot span processor groups. However, since Windows 10 Build 20348 the behavior changed,
            // so there's a small window of versions between this and Windows 11 that might exhibit problems
            // with not all processors being utilized.
            //
            // Handle this in NumaConfig::from_system by manually splitting the nodes when detect
            // that there is no function to set affinity spanning processor nodes.
            // This is required because otherwise thread distribution code may produce suboptimal results.
            //
            // See https://learn.microsoft.com/en-us/windows/win32/procthread/numa-support
            GROUP_AFFINITY groupAffinity;
            std::memset(&groupAffinity, 0, sizeof(groupAffinity));
            // Use an ordered set so guaranteed to get the smallest cpu number here.
            std::size_t forcedProcGroupIndex = *(nodes[numaIdx].begin()) / WIN_PROCESSOR_GROUP_SIZE;
            groupAffinity.Group              = static_cast<WORD>(forcedProcGroupIndex);
            for (CpuIndex cpuIdx : nodes[numaIdx])
            {
                std::size_t procGroupIndex   = cpuIdx / WIN_PROCESSOR_GROUP_SIZE;
                std::size_t inProcGroupIndex = cpuIdx % WIN_PROCESSOR_GROUP_SIZE;
                // Skip processors that are not in the same processor group.
                // If everything was set up correctly this will never be an issue,
                // but have to account for bad NUMA node specification.
                if (procGroupIndex != forcedProcGroupIndex)
                    continue;

                groupAffinity.Mask |= KAFFINITY(1) << inProcGroupIndex;
            }

            threadHandle = GetCurrentThread();

            status = SetThreadGroupAffinity(threadHandle, &groupAffinity, nullptr);
            if (status == 0)
                std::exit(EXIT_FAILURE);

            // Yield this thread just to be sure it gets rescheduled.
            // This is defensive, allowed because this code is not performance critical.
            SwitchToThread();
        }

#endif

        return NumaReplicatedAccessToken(numaIdx);
    }

    template<typename FuncT>
    void execute_on_numa_node(NumaIndex numaIdx, FuncT&& f) const noexcept {
        std::thread th([this, &f, numaIdx]() {
            bind_current_thread_to_numa_node(numaIdx);
            std::forward<FuncT>(f)();
        });

        th.join();
    }

    std::vector<std::set<CpuIndex>>         nodes;
    std::unordered_map<CpuIndex, NumaIndex> nodeByCpu;

   private:
    static NumaConfig empty() noexcept { return NumaConfig(0, false); }

    static std::vector<CpuIndex> shortened_string_to_indices(std::string_view str) noexcept {
        std::vector<CpuIndex> indices;

        if (is_whitespace(str))
            return indices;

        for (auto ss : split(str, ","))
        {
            if (is_whitespace(ss))
                continue;

            auto parts = split(ss, "-");

            if (parts.size() == 1)
            {
                auto cpuIdx = CpuIndex{str_to_size_t(parts[0])};
                indices.emplace_back(cpuIdx);
            }
            else if (parts.size() == 2)
            {
                auto fstCpuIdx = CpuIndex{str_to_size_t(parts[0])};
                auto lstCpuIdx = CpuIndex{str_to_size_t(parts[1])};
                for (auto cpuIdx = fstCpuIdx; cpuIdx <= lstCpuIdx; ++cpuIdx)
                    indices.emplace_back(cpuIdx);
            }
            else
                assert(false);
        }

        return indices;
    }

    void remove_empty_numa_nodes() noexcept {
        std::vector<std::set<CpuIndex>> newNodes;

        for (auto&& cpus : nodes)
            if (!cpus.empty())
                newNodes.emplace_back(std::move(cpus));

        nodes = std::move(newNodes);
    }

    // Returns true if successful
    // Returns false if failed, i.e. when the cpu is already present
    //                          strong guarantee, the structure remains unmodified
    bool add_cpu_to_node(NumaIndex numaIdx, CpuIndex cpuIdx) noexcept {
        if (is_cpu_assigned(cpuIdx))
            return false;

        while (nodes_size() <= numaIdx)
            nodes.emplace_back();

        nodes[numaIdx].insert(cpuIdx);
        nodeByCpu[cpuIdx] = numaIdx;

        if (maxCpuIndex < cpuIdx)
            maxCpuIndex = cpuIdx;

        return true;
    }

    // Returns true if successful.
    // Returns false if failed.
    // i.e. when any of the cpus is already present strong guarantee, the structure remains unmodified.
    bool add_cpu_range_to_node(NumaIndex numaIdx, CpuIndex fstCpuIdx, CpuIndex lstCpuIdx) noexcept {
        for (auto cpuIdx = fstCpuIdx; cpuIdx <= lstCpuIdx; ++cpuIdx)
            if (is_cpu_assigned(cpuIdx))
                return false;

        while (nodes_size() <= numaIdx)
            nodes.emplace_back();

        for (auto cpuIdx = fstCpuIdx; cpuIdx <= lstCpuIdx; ++cpuIdx)
        {
            nodes[numaIdx].insert(cpuIdx);
            nodeByCpu[cpuIdx] = numaIdx;
        }

        if (maxCpuIndex < lstCpuIdx)
            maxCpuIndex = lstCpuIdx;

        return true;
    }

    CpuIndex maxCpuIndex;
    bool     affinityCustom;
};

class NumaReplicationContext;

// Instances of this class are tracked by the NumaReplicationContext instance.
// NumaReplicationContext informs all tracked instances when NUMA configuration changes.
class BaseNumaReplicated {
   public:
    BaseNumaReplicated(NumaReplicationContext& ctx) noexcept;

    BaseNumaReplicated(const BaseNumaReplicated&) noexcept = delete;
    BaseNumaReplicated(BaseNumaReplicated&& baseNumaRep) noexcept;
    BaseNumaReplicated& operator=(const BaseNumaReplicated&) noexcept = delete;
    BaseNumaReplicated& operator=(BaseNumaReplicated&& baseNumaRep) noexcept;
    virtual ~BaseNumaReplicated() noexcept;

    virtual void on_numa_config_changed() noexcept = 0;

    const NumaConfig& numa_config() const noexcept;

   private:
    NumaReplicationContext* context;
};

// Force boxing with a unique_ptr. If this becomes an issue due to added
// indirection may need to add an option for a custom boxing type.
// When the NUMA config changes the value stored at the index 0 is replicated to other nodes.
template<typename T>
class NumaReplicated final: public BaseNumaReplicated {
   public:
    explicit NumaReplicated(NumaReplicationContext& ctx) noexcept :
        BaseNumaReplicated(ctx) {
        replicate_from(T{});
    }

    NumaReplicated(NumaReplicationContext& ctx, T&& source) noexcept :
        BaseNumaReplicated(ctx) {
        replicate_from(std::move(source));
    }

    NumaReplicated(const NumaReplicated&) noexcept = delete;
    NumaReplicated(NumaReplicated&& numaRep) noexcept :
        BaseNumaReplicated(std::move(numaRep)),
        instances(std::exchange(numaRep.instances, {})) {}

    NumaReplicated& operator=(const NumaReplicated&) noexcept = delete;
    NumaReplicated& operator=(NumaReplicated&& numaRep) noexcept {
        if (this == &numaRep)
            return *this;
        BaseNumaReplicated::operator=(std::move(numaRep));
        instances = std::exchange(numaRep.instances, {});
        return *this;
    }

    NumaReplicated& operator=(T&& source) noexcept {
        replicate_from(std::move(source));
        return *this;
    }

    ~NumaReplicated() noexcept override = default;

    const T& operator[](NumaReplicatedAccessToken token) const noexcept {
        assert(token.numa_index() < instances.size());
        return *(instances[token.numa_index()]);
    }

    const T& operator*() const noexcept { return *(instances[0]); }

    const T* operator->() const noexcept { return instances[0].get(); }

    template<typename Func>
    void modify_and_replicate(Func&& f) noexcept {
        auto source = std::move(instances[0]);
        std::forward<Func>(f)(*source);
        replicate_from(std::move(*source));
    }

    void on_numa_config_changed() noexcept override {
        // Use the first one as the source. It doesn't matter which one use,
        // because they all must be identical, but the first one is guaranteed to exist.
        auto source = std::move(instances[0]);
        replicate_from(std::move(*source));
    }

   private:
    void replicate_from(T&& source) noexcept {
        instances.clear();

        const auto& numaCfg = numa_config();
        if (numaCfg.requires_memory_replication())
        {
            for (NumaIndex numaIdx = 0; numaIdx < numaCfg.nodes_size(); ++numaIdx)
                numaCfg.execute_on_numa_node(numaIdx, [this, &source]() {
                    instances.emplace_back(std::make_unique<T>(source));
                });
        }
        else
        {
            assert(numaCfg.nodes_size() == 1);
            // Take advantage of the fact that replication is not required
            // and reuse the source value, avoiding one copy operation.
            instances.emplace_back(std::make_unique<T>(std::move(source)));
        }
    }

    std::vector<std::unique_ptr<T>> instances;
};

// Force boxing with a unique_ptr. If this becomes an issue due to added
// indirection may need to add an option for a custom boxing type.
template<typename T>
class LazyNumaReplicated final: public BaseNumaReplicated {
   public:
    explicit LazyNumaReplicated(NumaReplicationContext& ctx) noexcept :
        BaseNumaReplicated(ctx) {
        prepare_replicate_from(T{});
    }

    LazyNumaReplicated(NumaReplicationContext& ctx, T&& source) noexcept :
        BaseNumaReplicated(ctx) {
        prepare_replicate_from(std::move(source));
    }

    LazyNumaReplicated(const LazyNumaReplicated&) noexcept = delete;
    LazyNumaReplicated(LazyNumaReplicated&& lazyNumaRep) noexcept :
        BaseNumaReplicated(std::move(lazyNumaRep)),
        instances(std::exchange(lazyNumaRep.instances, {})) {}

    LazyNumaReplicated& operator=(const LazyNumaReplicated&) noexcept = delete;
    LazyNumaReplicated& operator=(LazyNumaReplicated&& lazyNumaRep) noexcept {
        if (this == &lazyNumaRep)
            return *this;
        BaseNumaReplicated::operator=(std::move(lazyNumaRep));
        instances = std::exchange(lazyNumaRep.instances, {});
        return *this;
    }

    LazyNumaReplicated& operator=(T&& source) noexcept {
        prepare_replicate_from(std::move(source));
        return *this;
    }

    ~LazyNumaReplicated() noexcept override = default;

    const T& operator[](NumaReplicatedAccessToken token) const noexcept {
        assert(token.numa_index() < instances.size());
        ensure_present(token.numa_index());
        return *(instances[token.numa_index()]);
    }

    const T& operator*() const noexcept { return *(instances[0]); }

    const T* operator->() const noexcept { return instances[0].get(); }

    template<typename Func>
    void modify_and_replicate(Func&& f) noexcept {
        auto source = std::move(instances[0]);
        std::forward<Func>(f)(*source);
        prepare_replicate_from(std::move(*source));
    }

    void on_numa_config_changed() noexcept override {
        // Use the first one as the source. It doesn't matter which one use,
        // because they all must be identical, but the first one is guaranteed to exist.
        auto source = std::move(instances[0]);
        prepare_replicate_from(std::move(*source));
    }

   private:
    void ensure_present(NumaIndex numaIdx) const noexcept {
        assert(numaIdx < instances.size());

        if (instances[numaIdx] != nullptr)
            return;

        assert(numaIdx != 0);

        std::unique_lock uniqueLock(mutex);
        // Check again for races.
        if (instances[numaIdx] != nullptr)
            return;

        const auto& numaCfg = numa_config();
        numaCfg.execute_on_numa_node(
          numaIdx, [this, numaIdx]() { instances[numaIdx] = std::make_unique<T>(*instances[0]); });
    }

    void prepare_replicate_from(T&& source) noexcept {
        instances.clear();

        const auto& numaCfg = numa_config();
        if (numaCfg.requires_memory_replication())
        {
            assert(numaCfg.nodes_size() > 0);

            // Just need to make sure the first instance is there.
            // Note that cannot move here as need to reallocate the data
            // on the correct NUMA node.
            numaCfg.execute_on_numa_node(
              0, [this, &source]() { instances.emplace_back(std::make_unique<T>(source)); });

            // Prepare others for lazy init.
            instances.resize(numaCfg.nodes_size());
        }
        else
        {
            assert(numaCfg.nodes_size() == 1);
            // Take advantage of the fact that replication is not required
            // and reuse the source value, avoiding one copy operation.
            instances.emplace_back(std::make_unique<T>(std::move(source)));
        }
    }

    mutable std::vector<std::unique_ptr<T>> instances;
    mutable std::mutex                      mutex;
};

// Utilizes shared memory.
template<typename T>
class SystemWideLazyNumaReplicated final: public BaseNumaReplicated {
   public:
    SystemWideLazyNumaReplicated(NumaReplicationContext& ctx) noexcept :
        BaseNumaReplicated(ctx) {
        prepare_replicate_from(std::make_unique<T>());
    }

    SystemWideLazyNumaReplicated(NumaReplicationContext& ctx, std::unique_ptr<T>&& source) noexcept
        :
        BaseNumaReplicated(ctx) {
        prepare_replicate_from(std::move(source));
    }

    SystemWideLazyNumaReplicated(const SystemWideLazyNumaReplicated&) noexcept = delete;
    SystemWideLazyNumaReplicated(SystemWideLazyNumaReplicated&& sysNumaRep) noexcept :
        BaseNumaReplicated(std::move(sysNumaRep)),
        instances(std::exchange(sysNumaRep.instances, {})) {}

    SystemWideLazyNumaReplicated& operator=(const SystemWideLazyNumaReplicated&) noexcept = delete;
    SystemWideLazyNumaReplicated& operator=(SystemWideLazyNumaReplicated&& sysNumaRep) noexcept {
        if (this == &sysNumaRep)
            return *this;
        BaseNumaReplicated::operator=(std::move(sysNumaRep));
        instances = std::exchange(sysNumaRep.instances, {});
        return *this;
    }

    SystemWideLazyNumaReplicated& operator=(std::unique_ptr<T>&& source) noexcept {
        prepare_replicate_from(std::move(source));
        return *this;
    }

    ~SystemWideLazyNumaReplicated() noexcept override = default;

    const T& operator[](NumaReplicatedAccessToken token) const noexcept {
        assert(token.numa_index() < instances.size());
        ensure_present(token.numa_index());
        return *(instances[token.numa_index()]);
    }

    const T& operator*() const noexcept { return *(instances[0]); }

    const T* operator->() const noexcept { return &*instances[0]; }

    std::vector<std::pair<SystemWideSharedConstantAllocationStatus, std::optional<std::string>>>
    get_status_and_errors() const noexcept {
        std::vector<std::pair<SystemWideSharedConstantAllocationStatus, std::optional<std::string>>>
          status;
        status.reserve(instances.size());

        for (const auto& instance : instances)
            status.emplace_back(instance.get_status(), instance.get_error_message());

        return status;
    }

    template<typename FuncT>
    void modify_and_replicate(FuncT&& f) noexcept {
        auto source = std::make_unique<T>(*instances[0]);
        std::forward<FuncT>(f)(*source);
        prepare_replicate_from(std::move(source));
    }

    void on_numa_config_changed() noexcept override {
        // Use the first one as the source. It doesn't matter which one we use,
        // because they all must be identical, but the first one is guaranteed to exist.
        auto source = std::make_unique<T>(*instances[0]);
        prepare_replicate_from(std::move(source));
    }

   private:
    std::size_t get_discriminator(NumaIndex idx) const noexcept {
        const NumaConfig& cfg    = numa_config();
        const NumaConfig& sysCfg = NumaConfig::from_system(false);
        // as a discriminator, locate the hardware/system numa-domain this CpuIndex belongs to
        CpuIndex    cpu     = *cfg.nodes[idx].begin();  // get a CpuIndex from NumaIndex
        NumaIndex   sys_idx = sysCfg.is_cpu_assigned(cpu) ? sysCfg.nodeByCpu.at(cpu) : 0;
        std::string s       = sysCfg.to_string() + "$" + std::to_string(sys_idx);
        return std::hash<std::string>{}(s);
    }

    void ensure_present(NumaIndex idx) const noexcept {
        assert(idx < instances.size());

        if (instances[idx] != nullptr)
            return;

        assert(idx != 0);

        std::unique_lock<std::mutex> uniqueLock(mutex);
        // Check again for races.
        if (instances[idx] != nullptr)
            return;

        const NumaConfig& cfg = numa_config();
        cfg.execute_on_numa_node(idx, [this, idx]() {
            instances[idx] = SystemWideSharedConstant<T>(*instances[0], get_discriminator(idx));
        });
    }

    void prepare_replicate_from(std::unique_ptr<T>&& source) noexcept {
        instances.clear();

        const NumaConfig& cfg = numa_config();
        // We just need to make sure the first instance is there.
        // Note that we cannot move here as we need to reallocate the data
        // on the correct NUMA node.
        // Even in the case of a single NUMA node we have to copy since it's shared memory.
        if (cfg.requires_memory_replication())
        {
            assert(cfg.nodes_size() > 0);

            cfg.execute_on_numa_node(0, [this, &source]() {
                instances.emplace_back(SystemWideSharedConstant<T>(*source, get_discriminator(0)));
            });

            // Prepare others for lazy init.
            instances.resize(cfg.nodes_size());
        }
        else
        {
            assert(cfg.nodes_size() == 1);
            instances.emplace_back(SystemWideSharedConstant<T>(*source, get_discriminator(0)));
        }
    }

    mutable std::vector<SystemWideSharedConstant<T>> instances;
    mutable std::mutex                               mutex;
};

class NumaReplicationContext final {
   public:
    explicit NumaReplicationContext(NumaConfig&& numaCfg) noexcept :
        numaConfig(std::move(numaCfg)) {}

    NumaReplicationContext(const NumaReplicationContext&) noexcept            = delete;
    NumaReplicationContext(NumaReplicationContext&&) noexcept                 = delete;
    NumaReplicationContext& operator=(const NumaReplicationContext&) noexcept = delete;
    NumaReplicationContext& operator=(NumaReplicationContext&&) noexcept      = delete;

    ~NumaReplicationContext() noexcept {
        // The context must outlive replicated objects
        if (!trackedReplicated.empty())
            std::exit(EXIT_FAILURE);
    }

    void attach(BaseNumaReplicated* numaRep) noexcept {
        assert(trackedReplicated.count(numaRep) == 0);
        trackedReplicated.insert(numaRep);
    }

    void detach(BaseNumaReplicated* numaRep) noexcept {
        assert(trackedReplicated.count(numaRep) == 1);
        trackedReplicated.erase(numaRep);
    }

    // oldObj may be invalid at this point
    void move_attached(BaseNumaReplicated* oldNumaRep, BaseNumaReplicated* newNumaRep) noexcept {
        assert(trackedReplicated.count(oldNumaRep) == 1);
        assert(trackedReplicated.count(newNumaRep) == 0);
        trackedReplicated.erase(oldNumaRep);
        trackedReplicated.insert(newNumaRep);
    }

    void set_numa_config(NumaConfig&& numaCfg) noexcept {
        numaConfig = std::move(numaCfg);
        for (auto&& numaRep : trackedReplicated)
            numaRep->on_numa_config_changed();
    }

    const NumaConfig& numa_config() const noexcept { return numaConfig; }

   private:
    NumaConfig numaConfig;

    // std::set uses std::less by default, which is required for pointer comparison
    std::set<BaseNumaReplicated*> trackedReplicated;
};

inline BaseNumaReplicated::BaseNumaReplicated(NumaReplicationContext& ctx) noexcept :
    context(&ctx) {
    context->attach(this);
}

inline BaseNumaReplicated::BaseNumaReplicated(BaseNumaReplicated&& baseNumaRep) noexcept :
    context(std::exchange(baseNumaRep.context, nullptr)) {
    context->move_attached(&baseNumaRep, this);
}

inline BaseNumaReplicated&
BaseNumaReplicated::operator=(BaseNumaReplicated&& baseNumaRep) noexcept {
    context = std::exchange(baseNumaRep.context, nullptr);
    context->move_attached(&baseNumaRep, this);
    return *this;
}

inline BaseNumaReplicated::~BaseNumaReplicated() noexcept {
    if (context != nullptr)
        context->detach(this);
}

inline const NumaConfig& BaseNumaReplicated::numa_config() const noexcept {
    return context->numa_config();
}

}  // namespace DON

#endif  // #ifndef NUMA_H_INCLUDED
