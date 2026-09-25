/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

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
#include <cassert>
#include <cstdlib>  // exit(), EXIT_FAILURE
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>  // variant<>
#include <vector>

#if !defined(_WIN64)                                 /* Non-Windows */ \
  && ((defined(__linux__) && !defined(__ANDROID__))) /* Linux (Non-Android) */
    #define USE_UNIX_NUMA
#endif

#if defined(_WIN64)
    #include <type_traits>
#elif defined(USE_UNIX_NUMA)
#endif

#include "misc.h"
#include "native_thread.h"
#include "shm.h"

namespace DON {

using CpuVector    = std::vector<CpuIndex>;
using CpuToNodeMap = std::unordered_map<CpuIndex, NumaIndex>;
using CpuSet       = std::unordered_set<CpuIndex>;

CpuIndex hardware_concurrency() noexcept;

inline const CpuIndex SYSTEM_THREAD_MAX = std::max(hardware_concurrency(), CpuIndex{1});

inline const usize THREAD_MAX = std::clamp(usize(4 * SYSTEM_THREAD_MAX), 1 * KB, 64 * KB);

#if defined(_WIN64)
inline constexpr LPCSTR KERNEL_MODULE_NAME = TEXT("kernel32.dll");

// On Windows each processor group can have up to 64 processors.
// https://learn.microsoft.com/en-us/windows/win32/procthread/processor-groups
inline constexpr u16 WIN_PROCESSOR_GROUP_SIZE = 64;

// https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadselectedcpusetmasks
using GetThreadSelectedCpuSetMasks_ = BOOL(WINAPI*)(
  HANDLE          hThread,          // [in]  Thread handle
  PGROUP_AFFINITY CpuSetMasks,      // [out] CPU set masks array
  USHORT          CpuSetMaskCount,  // [in]  Masks array size
  PUSHORT         RequiredCount     // [out] Required mask array size
);
// https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setthreadselectedcpusetmasks
using SetThreadSelectedCpuSetMasks_ = BOOL(WINAPI*)(
  HANDLE          hThread,         // [in] Thread handle
  PGROUP_AFFINITY CpuSetMasks,     // [in] CPU set masks array
  USHORT          CpuSetMaskCount  // [in] Masks array size
);

struct WindowsAffinity final {
   public:
    std::optional<CpuSet> combined_cpus() const noexcept;

    // Since Windows 11 and Windows Server 2022 thread affinities can span
    // processor groups and can be set as such by a new WinAPI function.
    // However, may need to force using the old API if detect that the process has
    // affinity set by the old API already and want to override that.
    // Due to the limitations of the old API cannot detect its use reliably.
    // There will be cases where detect not use but it has actually been used and vice versa.
    bool likely_use_cpus(usize idx) const noexcept;

    // Also provide diagnostic for when the affinity is set to nullopt whether it was due to being indeterminate.
    // If affinity is indeterminate it is best to assume it is not set at all, so consistent with the meaning of the nullopt affinity.
    Array<bool, 2>   determinate{true, true};
    Array<CpuSet, 2> cpus;
};

std::pair<BOOL, std::vector<USHORT>> get_process_group_affinity() noexcept;

// On Windows there are two ways to set affinity, and therefore 2 ways to get it.
// These are not consistent, so have to check both. In some cases it is actually
// not possible to determine affinity. For example when two different threads have
// affinity on different processor groups, set using SetThreadAffinityMask,
// cannot retrieve the actual affinities.
// From documentation on GetProcessAffinityMask:
//   - If the calling process contains threads in multiple groups,
//   - If the function returns zero for both affinity masks.
// In such cases just give up and assume have affinity for all processors.
// nullopt means no affinity is set, that is, all processors are allowed
WindowsAffinity get_process_affinity() noexcept;

inline const auto PROCESSOR_AFFINITY = get_process_affinity();

inline const Array<bool, 2> LIKELY_USE_CPUS{
  PROCESSOR_AFFINITY.likely_use_cpus(0),
  PROCESSOR_AFFINITY.likely_use_cpus(1)  //
};

// Type machinery used to emulate Cache->GroupCount

template<typename T, typename = void>
struct HasGroupCount: std::bool_constant<false> {};

template<typename T>
struct HasGroupCount<T, std::void_t<decltype(std::declval<T>().Cache.GroupCount)>>
    : std::bool_constant<true> {};

template<typename T, typename Pred>
CpuSet read_cache_members(const T* processorInfo, Pred&& is_cpu_allowed) noexcept {
    CpuSet cpus;

    const auto add_group_cpus = [&](WORD groupId, KAFFINITY groupMask) noexcept {
        for (u16 number = 0; number < WIN_PROCESSOR_GROUP_SIZE; ++number)
        {
            if ((groupMask & bit(u8(number))) != 0)
            {
                const CpuIndex cpuId = groupId * WIN_PROCESSOR_GROUP_SIZE + number;

                if (is_cpu_allowed(cpuId))
                    cpus.insert(cpuId);
            }
        }
    };

    // Handle types with Cache.GroupCount
    if constexpr (HasGroupCount<T>::value)
    {
        // On Windows 10 this will read a 0 because GroupCount doesn't exist
        const WORD groupCount = std::max(processorInfo->Cache.GroupCount, WORD{1});

        for (WORD i = 0; i < groupCount; ++i)
        {
            const WORD      groupId   = processorInfo->Cache.GroupMasks[i].Group;
            const KAFFINITY groupMask = processorInfo->Cache.GroupMasks[i].Mask;

            add_group_cpus(groupId, groupMask);
        }
    }
    // Handle types without Cache.GroupCount
    else
    {
        const WORD      groupId   = processorInfo->Cache.GroupMask.Group;
        const KAFFINITY groupMask = processorInfo->Cache.GroupMask.Mask;

        add_group_cpus(groupId, groupMask);
    }

    return cpus;
}

#elif defined(USE_UNIX_NUMA)
CpuSet get_process_affinity() noexcept;

inline const auto PROCESSOR_AFFINITY = get_process_affinity();
#endif

// Want to abstract the purpose of storing the numa node index somewhat.
// Whoever is using this does not need to know the specifics of the replication
// machinery to be able to access NUMA replicated memory.
class NumaReplicatedAccessToken final {
   public:
    NumaReplicatedAccessToken() noexcept;

    explicit NumaReplicatedAccessToken(NumaIndex numaIdx) noexcept;

    NumaIndex numa_id() const noexcept;

   private:
    NumaIndex numaId;
};

struct L3Domain final {
   public:
    NumaIndex sysNumaId;
    CpuSet    cpus;
};

// Use system-reported NUMA nodes
struct SystemNumaPolicy {};
// Use system-reported L3 domains
struct L3DomainsPolicy {};
// Group system-reported L3 domains into bundles up to bundleSize
struct BundledL3Policy {
   public:
    usize bundleSize;
};

// Automatically select the NUMA policy
using AutoNumaPolicy = std::variant<SystemNumaPolicy, L3DomainsPolicy, BundledL3Policy>;

// The default configuration will attempt to group L3 domains up to 32 threads.
// This size was found to be a good balance between the Elo gain of increased
// history sharing and the speed loss from more cross-cache accesses.
// The user can always explicitly override this behavior.
inline constexpr AutoNumaPolicy NUMA_POLICY_DEFAULT = BundledL3Policy{32};

CpuVector parse_to_cpus(std::string_view sv) noexcept;

// Designed as immutable, because there is no good reason to alter an already
// existing config in a way that doesn't require recreating it completely, and
// it would be complex and expensive to maintain class invariants.
// The CPU (processor) numbers always correspond to the actual numbering used
// by the system. The NUMA node numbers MAY NOT correspond to the system's
// numbering of the NUMA nodes. In particular, by default, if the processor has
// non-uniform cache access within a NUMA node (i.e., a non-unified L3 cache structure),
// then L3 domains within a system NUMA node will be used to subdivide it
// into multiple logical NUMA nodes in the config. Additionally, empty nodes may
// be removed, or the user may create custom nodes.
//
// As a special case, when performing system-wide replication of read-only data
// (i.e., LazyNumaReplicatedSystemWide), the system NUMA node is used, rather than
// custom or L3-aware nodes. See that class's discriminator_hash() function.
//
// It is guaranteed that NUMA nodes are NOT empty: every node exposed by NumaConfig
// has at least one processor assigned.
//
// Use startup affinities so as not to modify its own behavior in time.
//
// Since DON doesn't support exceptions all places where an exception
// should be thrown are replaced by std::exit.
class NumaConfig final {
   public:
    static NumaConfig empty() noexcept;

    // This function gets a NumaConfig based on the system's provided information.
    // The available policies are documented above.
    static NumaConfig from_system(const AutoNumaPolicy& numaPolicy,
                                  bool                  respectProcessAffinity = true) noexcept;
    // ':'-separated numa nodes
    // ','-separated cpu indices
    // supports "first-last" range syntax for cpu indices
    // For example:
    // "0-7:8-15:16-23:24-31"
    // "0-15,128-143:16-31,144-159:32-47,160-175:48-63,176-191"
    static std::optional<NumaConfig> from_string(std::string_view str) noexcept;

    NumaConfig(CpuIndex maxCpuIdx, bool customAff) noexcept;

    NumaConfig() noexcept;

    NumaConfig(const NumaConfig&) noexcept            = delete;
    NumaConfig& operator=(const NumaConfig&) noexcept = delete;
    NumaConfig(NumaConfig&&) noexcept                 = default;
    NumaConfig& operator=(NumaConfig&&) noexcept      = default;

    usize nodes_size() const noexcept;

    CpuVector&       node_cpus(NumaIndex numaId) noexcept;
    const CpuVector& node_cpus(NumaIndex numaId) const noexcept;

    bool node_cpus_empty(NumaIndex numaId) const noexcept;

    usize node_cpus_size(NumaIndex numaId) const noexcept;

    CpuIndex node_cpus_front(const NumaIndex numaId) const noexcept;

    usize cpus_size() const noexcept;

    bool is_cpu_assigned(CpuIndex cpuId) const noexcept;

    NumaIndex node_by_cpu(CpuIndex cpuId) const noexcept;

    bool requires_memory_replication() const noexcept;

    // Format: "node0_cpus:node1_cpus:..." where cpus = "0-2,4,6-7"
    std::string to_string() const noexcept;

    bool suggests_binding_threads(u16 threadCount) const noexcept;

    std::vector<NumaIndex> distribute_threads_among_numa_nodes(u16 threadCount) const noexcept;

    NumaReplicatedAccessToken bind_current_thread_to_numa_node(NumaIndex numaId) const noexcept;

    template<typename Func>
    void execute_on_numa_node(const NumaIndex numaId, Func&& f) const noexcept {

        NativeThread nativeThread =
          create_native_thread([this, f = std::forward<Func>(f), numaId]() mutable noexcept {
              [[maybe_unused]] const auto token = bind_current_thread_to_numa_node(numaId);
              f();
          });

        if (!nativeThread.joinable())
        {
            std::cerr << "Failed to create native thread on NUMA node" << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }

   private:
    // This function queries the system for the mapping of processors to NUMA nodes.
    // On Linux read from standardized kernel sysfs, with a fallback to single NUMA node.
    // On Windows utilize GetNumaProcessorNodeEx, which has its quirks,
    // see comment for Windows implementation of get_process_affinity.
    template<typename Pred>
    static NumaConfig from_system_numa([[maybe_unused]] const bool respectProcessAffinity,
                                       [[maybe_unused]] Pred&&     is_cpu_allowed) noexcept {
        NumaConfig numaCfg = empty();

#if defined(_WIN64)

        const WORD activeProcGroupCount = ::GetActiveProcessorGroupCount();

        for (WORD groupId = 0; groupId < activeProcGroupCount; ++groupId)
        {
            const u16 activeProcCount = u16(::GetActiveProcessorCount(groupId));
            const u16 processorCount  = std::max(activeProcCount, WIN_PROCESSOR_GROUP_SIZE);
            // number == processorIndex
            for (u16 number = 0; number < processorCount; ++number)
            {
                PROCESSOR_NUMBER processorNumber{};
                processorNumber.Group  = groupId;
                processorNumber.Number = BYTE(number);
                //processorNumber.Reserved = 0;

                USHORT nodeNumber;

                if (::GetNumaProcessorNodeEx(&processorNumber, &nodeNumber) == TRUE
                    && nodeNumber != USHORT{0xFFFF})  // std::numeric_limits<USHORT>::max()
                {
                    const CpuIndex cpuId = groupId * WIN_PROCESSOR_GROUP_SIZE + number;

                    if (is_cpu_allowed(cpuId))
                        numaCfg.add_cpu_to_node(nodeNumber, cpuId);
                }
            }
        }

#elif defined(USE_UNIX_NUMA)

        // On Linux things are straightforward, since there's no processor groups
        // and any thread can be scheduled on all processors.
        // Try to gather this information from the sysfs first
        // https://www.kernel.org/doc/Documentation/ABI/stable/sysfs-devices-node

        bool useFallback = false;

        // /sys/devices/system/node/online contains information about active NUMA nodes
        auto nodeStr = read_file_to_string("/sys/devices/system/node/online");

        if (!nodeStr || nodeStr->empty())
        {
            useFallback = true;
        }
        else
        {
            *nodeStr = remove_whitespace(*nodeStr);

            for (const NumaIndex nodeId : parse_to_cpus(*nodeStr))
            {
                // /sys/devices/system/node/node.../cpulist
                const auto path = std::string{"/sys/devices/system/node/node"}
                                + std::to_string(nodeId) + "/cpulist";

                auto cpusStr = read_file_to_string(path);

                // Now, only bail if the file does not exist. Some nodes may be
                // empty, that's fine. An empty node still has a file that appears
                // to have some whitespace, so need to handle that.
                if (!cpusStr)
                {
                    useFallback = true;
                    break;
                }
                else
                {
                    *cpusStr = remove_whitespace(*cpusStr);

                    for (const CpuIndex cpuId : parse_to_cpus(*cpusStr))
                        if (is_cpu_allowed(cpuId))
                            numaCfg.add_cpu_to_node(nodeId, cpuId);
                }
            }
        }

        if (useFallback)
        {
            numaCfg = empty();

            for (CpuIndex cpuId = 0; cpuId < SYSTEM_THREAD_MAX; ++cpuId)
                if (is_cpu_allowed(cpuId))
                    numaCfg.add_cpu_to_node(NumaIndex{0}, cpuId);
        }
#endif

        return numaCfg;
    }

    template<typename Pred>
    static std::optional<NumaConfig>
    try_l3_domain(const bool              respectProcessAffinity,
                  const usize             bundleSize,
                  [[maybe_unused]] Pred&& is_cpu_allowed) noexcept {
        // Get the normal system configuration so that know to which NUMA node each L3 domain belongs
        NumaConfig sysCfg = NumaConfig::from_system(SystemNumaPolicy{}, respectProcessAffinity);

        std::vector<L3Domain> l3Domains;

#if defined(_WIN64)

        DWORD bufSize = 0;

        ::GetLogicalProcessorInformationEx(LOGICAL_PROCESSOR_RELATIONSHIP::RelationCache, nullptr,
                                           &bufSize);

        if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            return std::nullopt;

        std::vector<char> buffer(bufSize);

        auto* processorInfo =
          reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());

        if (!::GetLogicalProcessorInformationEx(LOGICAL_PROCESSOR_RELATIONSHIP::RelationCache,
                                                processorInfo, &bufSize))
            return std::nullopt;

        while (reinterpret_cast<char*>(processorInfo) < buffer.data() + bufSize)
        {
            processorInfo = std::launder(processorInfo);

            if (processorInfo->Relationship == LOGICAL_PROCESSOR_RELATIONSHIP::RelationCache
                && processorInfo->Cache.Level == BYTE{3})
            {
                L3Domain l3Domain{};

                l3Domain.cpus = read_cache_members(processorInfo, is_cpu_allowed);

                if (!l3Domain.cpus.empty())
                {
                    l3Domain.sysNumaId = sysCfg.node_by_cpu(*l3Domain.cpus.begin());

                    l3Domains.push_back(std::move(l3Domain));
                }
            }

            // Variable length data structure, advance to next
            processorInfo = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(
              reinterpret_cast<char*>(processorInfo) + processorInfo->Size);
        }

#elif defined(USE_UNIX_NUMA)

        CpuSet seenCpus;

        for (const auto& [nextCpuId, _] : sysCfg.cpuToNode)
        {
            if (seenCpus.find(nextCpuId) != seenCpus.end())
                continue;

            const auto path = std::string{"/sys/devices/system/cpu/cpu"} + std::to_string(nextCpuId)
                            + "/cache/index3/shared_cpu_list";

            auto cpusStr = read_file_to_string(path);

            if (!cpusStr || cpusStr->empty())
                continue;

            *cpusStr = remove_whitespace(*cpusStr);

            L3Domain l3Domain{};

            for (const CpuIndex cpuId : parse_to_cpus(*cpusStr))
            {
                if (is_cpu_allowed(cpuId))
                {
                    l3Domain.sysNumaId = sysCfg.node_by_cpu(cpuId);
                    l3Domain.cpus.insert(cpuId);
                }

                seenCpus.insert(cpuId);
            }

            if (!l3Domain.cpus.empty())
                l3Domains.emplace_back(std::move(l3Domain));
        }
#endif

        if (!l3Domains.empty())
            return NumaConfig::from_l3_domain(std::move(l3Domains), bundleSize);

        return std::nullopt;
    }

    static NumaConfig from_l3_domain(std::vector<L3Domain> l3Domains, usize bundleSize) noexcept;

    void resize_numa_node(usize newNumaId) noexcept;

    void add_numa_node_cpu(NumaIndex numaId, CpuIndex cpuId) noexcept;

    void add_numa_node(NumaIndex numaId, CpuIndex cpuId) noexcept;

    // Returns true if successful, false if failed.
    // i.e. when the cpu is already present strong guarantee, the structure remains unmodified.
    bool add_cpu_to_node(NumaIndex numaId, CpuIndex cpuId) noexcept;

    // Returns true if successful, false if failed.
    // i.e. when any of the cpus is already present strong guarantee, the structure remains unmodified.
    bool add_cpu_range_to_node(NumaIndex numaId, CpuIndex cpuIdBeg, CpuIndex cpuIdEnd) noexcept;

    // Removes empty NUMA nodes and rebuilds CPU-to-NUMA mappings.
    void remove_empty_numa_nodes() noexcept;

    std::vector<CpuVector> nodes;
    CpuToNodeMap           cpuToNode;
    CpuIndex               maxCpuId;
    bool                   customAffinity;
};

class BaseNumaReplicated;

class NumaReplicationContext final {
   public:
    explicit NumaReplicationContext(NumaConfig&& numaCfg) noexcept;

    NumaReplicationContext(const NumaReplicationContext&) noexcept            = delete;
    NumaReplicationContext& operator=(const NumaReplicationContext&) noexcept = delete;

    NumaReplicationContext(NumaReplicationContext&&) noexcept            = delete;
    NumaReplicationContext& operator=(NumaReplicationContext&&) noexcept = delete;

    ~NumaReplicationContext() noexcept;

    bool attach(BaseNumaReplicated* numaRep) noexcept;
    bool detach(BaseNumaReplicated* numaRep) noexcept;

    // oldNumaRep may be invalid at this point.
    bool move(BaseNumaReplicated* oldNumaRep, BaseNumaReplicated* newNumaRep) noexcept;

    void set_numa_config(NumaConfig&& numaCfg) noexcept;

    const NumaConfig& numa_config() const noexcept;

   private:
    NumaConfig numaConfig;

    std::unordered_set<BaseNumaReplicated*> replicatedSet;
};

// Instances of this class are tracked by the NumaReplicationContext instance.
// NumaReplicationContext informs all tracked instances when NUMA configuration changes.
class BaseNumaReplicated {
   public:
    explicit BaseNumaReplicated(NumaReplicationContext& numaCtx) noexcept;

    BaseNumaReplicated(const BaseNumaReplicated&) noexcept            = delete;
    BaseNumaReplicated& operator=(const BaseNumaReplicated&) noexcept = delete;

    BaseNumaReplicated(BaseNumaReplicated&& baseNumaRep) noexcept;
    BaseNumaReplicated& operator=(BaseNumaReplicated&& baseNumaRep) noexcept;

    virtual ~BaseNumaReplicated() noexcept;

    [[nodiscard]] const NumaConfig& numa_config() const noexcept;

    virtual void on_numa_config_changed() noexcept = 0;

   private:
    bool attach_context() noexcept;
    bool detach_context() noexcept;
    bool move_context(BaseNumaReplicated& baseNumaRep) noexcept;

    NumaReplicationContext* numaContext;
};

// Force boxing with a unique_ptr. If this becomes an issue due to added
// indirection may need to add an option for a custom boxing type.
// When the NUMA config changes the value stored at the index 0 is replicated to other nodes.
template<typename T>
class NumaReplicated final: public BaseNumaReplicated {
   public:
    explicit NumaReplicated(NumaReplicationContext& ctx) noexcept :
        BaseNumaReplicated{ctx} {
        replicate_from(T{});
    }

    NumaReplicated(NumaReplicationContext& ctx, T&& source) noexcept :
        BaseNumaReplicated{ctx} {
        replicate_from(std::move(source));
    }

    NumaReplicated(const NumaReplicated&) noexcept            = delete;
    NumaReplicated& operator=(const NumaReplicated&) noexcept = delete;

    NumaReplicated(NumaReplicated&& numaRep) noexcept :
        BaseNumaReplicated(std::move(numaRep)),
        instances(std::exchange(numaRep.instances, {})) {}
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

    const T& operator[](const NumaReplicatedAccessToken token) const noexcept {
        assert(token.numa_id() < instances.size());

        return *(instances[token.numa_id()]);
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
            for (usize numaId = 0; numaId < numaCfg.nodes_size(); ++numaId)
                numaCfg.execute_on_numa_node(NumaIndex(numaId), [this, &source]() {
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
        BaseNumaReplicated{ctx} {
        prepare_replicate_from(T{});
    }

    LazyNumaReplicated(NumaReplicationContext& ctx, T&& source) noexcept :
        BaseNumaReplicated{ctx} {
        prepare_replicate_from(std::move(source));
    }

    LazyNumaReplicated(const LazyNumaReplicated&) noexcept            = delete;
    LazyNumaReplicated& operator=(const LazyNumaReplicated&) noexcept = delete;

    LazyNumaReplicated(LazyNumaReplicated&& lazyNumaRep) noexcept :
        BaseNumaReplicated(std::move(lazyNumaRep)),
        instances(std::exchange(lazyNumaRep.instances, {})) {}
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

    const T& operator[](const NumaReplicatedAccessToken token) const noexcept {
        assert(token.numa_id() < instances.size());

        ensure_present(token.numa_id());

        return *(instances[token.numa_id()]);
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
    void ensure_present(const NumaIndex numaId) const noexcept {
        assert(numaId < instances.size());

        if (instances[numaId] != nullptr)
            return;

        assert(numaId != 0);

        std::lock_guard writeLock(mutex);

        // Check again for races.
        if (instances[numaId] != nullptr)
            return;

        const auto& numaCfg = numa_config();

        numaCfg.execute_on_numa_node(numaId, [this, numaId]() noexcept -> void {
            instances[numaId] = std::make_unique<T>(*instances[0]);
        });
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
            numaCfg.execute_on_numa_node(0, [this, &source]() noexcept -> void {
                instances.emplace_back(std::make_unique<T>(source));
            });

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

    mutable std::mutex                      mutex;
    mutable std::vector<std::unique_ptr<T>> instances;
};

// Utilizes shared memory
template<typename T>
class SystemWideLazyNumaReplicated final: public BaseNumaReplicated {
   public:
    SystemWideLazyNumaReplicated(NumaReplicationContext& ctx, std::unique_ptr<T>&& source) noexcept
        :
        BaseNumaReplicated{ctx} {
        prepare_replicate_from(std::move(source));
    }

    SystemWideLazyNumaReplicated(const SystemWideLazyNumaReplicated&) noexcept            = delete;
    SystemWideLazyNumaReplicated& operator=(const SystemWideLazyNumaReplicated&) noexcept = delete;

    SystemWideLazyNumaReplicated(SystemWideLazyNumaReplicated&& sysNumaRep) noexcept :
        BaseNumaReplicated(std::move(sysNumaRep)),
        instances(std::exchange(sysNumaRep.instances, {})) {}
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

    const T& operator[](const NumaReplicatedAccessToken token) const noexcept {
        assert(token.numa_id() < instances.size());

        ensure_present(token.numa_id());

        return *(instances[token.numa_id()]);
    }

    const T& operator*() const noexcept { return *(instances[0]); }

    const T* operator->() const noexcept { return &*instances[0]; }

    auto get_status_and_errors() const noexcept {
        std::vector<std::pair<SharedMemoryAllocationStatus, std::string_view>> status;
        status.reserve(instances.size());

        for (const auto& instance : instances)
            status.emplace_back(instance.get_status(), instance.get_error_message());

        return status;
    }

    template<typename Func>
    void modify_and_replicate(Func&& f) noexcept {
        auto source = std::make_unique<T>(*instances[0]);
        std::forward<Func>(f)(*source);
        prepare_replicate_from(std::move(source));
    }

    void on_numa_config_changed() noexcept override {
        // Use the first one as the source. It doesn't matter which one used,
        // because they all must be identical, but the first one is guaranteed to exist.
        auto source = std::make_unique<T>(*instances[0]);
        prepare_replicate_from(std::move(source));
    }

   private:
    u64 discriminator_hash(const NumaIndex numaId) const noexcept {

        const NumaConfig& numaCfg = numa_config();
        const NumaConfig  sysCfg  = NumaConfig::from_system(SystemNumaPolicy{}, false);

        // Map a CPU from the configured NUMA node to its hardware/system NUMA domain.
        const CpuIndex  cpuId     = numaCfg.node_cpus_front(numaId);
        const NumaIndex sysNumaId = sysCfg.node_by_cpu(cpuId);
        const auto discriminator = sysCfg.to_string().append("$").append(std::to_string(sysNumaId));

        return hash_string(discriminator);
    }

    void ensure_present(const NumaIndex numaId) const noexcept {
        assert(numaId < instances.size());

        if (instances[numaId] != nullptr)
            return;

        assert(numaId != 0);

        std::lock_guard writeLock(mutex);

        // Check again for races
        if (instances[numaId] != nullptr)
            return;

        const NumaConfig& numaCfg = numa_config();

        numaCfg.execute_on_numa_node(numaId, [this, numaId]() noexcept -> void {
            instances[numaId] =
              SystemWideSharedMemory<T>(*instances[0], discriminator_hash(numaId));
        });
    }

    void prepare_replicate_from(std::unique_ptr<T>&& source) noexcept {
        instances.clear();

        const NumaConfig& numaCfg = numa_config();
        // Just need to make sure the first instance is there.
        // Note that cannot move here as need to reallocate the data on the correct NUMA node.
        // Even in the case of a single NUMA node have to copy since it's shared memory.
        if (numaCfg.requires_memory_replication())
        {
            assert(numaCfg.nodes_size() != 0);

            numaCfg.execute_on_numa_node(0, [this, &source]() {
                instances.emplace_back(SystemWideSharedMemory<T>(*source, discriminator_hash(0)));
            });

            // Prepare others for lazy init
            instances.resize(numaCfg.nodes_size());
        }
        else
        {
            assert(numaCfg.nodes_size() == 1);

            instances.emplace_back(SystemWideSharedMemory<T>(*source, discriminator_hash(0)));
        }
    }

    mutable std::mutex                             mutex;
    mutable std::vector<SystemWideSharedMemory<T>> instances;
};

}  // namespace DON

#endif  // NUMA_H_INCLUDED
