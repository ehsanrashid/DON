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

#include "numa.h"

#include <cstdlib>
#include <iostream>

namespace DON {

CpuIndex hardware_concurrency() noexcept {
    CpuIndex concurrency = std::thread::hardware_concurrency();

    // Get all processors across all processor groups on windows, since
    // ::hardware_concurrency() only returns the number of processors in
    // the first group, because only these are available to std::thread.
#if defined(_WIN64)
    concurrency = CpuIndex(std::clamp<u32>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS),
                                           concurrency, std::numeric_limits<CpuIndex>::max()));
#endif

    return concurrency;
}

CpuIndexVec shortened_string_to_cpus(const std::string_view str) noexcept {
    CpuIndexVec cpus;

    if (is_whitespace(str))
        return cpus;

    for (const auto ss : split(str, ",", true))
    {
        if (is_whitespace(ss))
            continue;

        const auto parts = split(ss, "-", true);

        switch (parts.size())
        {
        case 1 : {
            const auto cpuId = str_to_usize(parts[0]);
            if (cpuId)
                cpus.emplace_back(CpuIndex(*cpuId));
        }
        break;
        case 2 : {
            // Limit expansion to 1M CPU IDs
            constexpr usize MaxCpus = 64 * KB;

            if (cpus.size() >= MaxCpus)
                break;

            const auto idBeg = str_to_usize(parts[0]);
            const auto idEnd = str_to_usize(parts[1]);

            if (idBeg && idEnd && *idBeg <= *idEnd && *idEnd - *idBeg < MaxCpus - cpus.size()
                && *idEnd <= std::numeric_limits<CpuIndex>::max())
            {
                const auto cpuIdBeg = static_cast<CpuIndex>(*idBeg);
                const auto cpuIdEnd = static_cast<CpuIndex>(*idEnd);

                for (CpuIndex cpuId = cpuIdBeg;; ++cpuId)
                {
                    cpus.emplace_back(cpuId);
                    if (cpuId == cpuIdEnd)
                        break;
                }
            }
        }
        break;
        default :
            assert(false);
            UNREACHABLE();
        }
    }

    return cpus;
}

NumaConfig NumaConfig::empty() noexcept { return NumaConfig{0, false}; }

NumaConfig NumaConfig::from_system([[maybe_unused]] const AutoNumaPolicy& numaPolicy,
                                   [[maybe_unused]] const bool respectProcessAffinity) noexcept {
    NumaConfig numaCfg = empty();

#if defined(_WIN64) || defined(USE_UNIX_NUMA)
    #if defined(_WIN64)
    std::optional<CpuIndexSet> allowedCpus;

    if (respectProcessAffinity)
        allowedCpus = PROCESSOR_AFFINITY.combined_cpus();

    // The affinity cannot be determined in all cases on Windows,
    // but at least guarantee that the number of allowed processors
    // is >= number of processors in the affinity mask. In case the user
    // is not satisfied they must set the processor numbers explicitly.
    const auto is_cpu_allowed = [&allowedCpus](CpuIndex cpuId) noexcept {
        return !allowedCpus || allowedCpus->find(cpuId) != allowedCpus->end();
    };

    #elif defined(USE_UNIX_NUMA)
    CpuIndexSet allowedCpus;

    if (respectProcessAffinity)
        allowedCpus = PROCESSOR_AFFINITY;

    const auto is_cpu_allowed = [&allowedCpus](CpuIndex cpuId) noexcept {
        return allowedCpus.find(cpuId) != allowedCpus.end();
    };

    #endif

    bool l3Success = false;

    if (!std::holds_alternative<SystemNumaPolicy>(numaPolicy))
    {
        usize l3BundleSize = 0;

        if (const auto* l3Policy = std::get_if<BundledL3Policy>(&numaPolicy))
            l3BundleSize = l3Policy->bundleSize;

        if (auto l3NumaCfg = try_l3_domain(respectProcessAffinity, l3BundleSize, is_cpu_allowed))
        {
            numaCfg = std::move(*l3NumaCfg);

            l3Success = true;
        }
    }

    if (!l3Success)
        numaCfg = from_system_numa(respectProcessAffinity, is_cpu_allowed);

    #if defined(_WIN64)
    // Split the NUMA nodes to be contained within a group if necessary.
    // This is needed between Windows 10 Build 20348 and Windows 11, because
    // the new NUMA allocation behavior was introduced while there was
    // still no way to set thread affinity spanning multiple processor groups.
    // See https://learn.microsoft.com/en-us/windows/win32/procthread/numa-support
    // Also do this if need to force old API for some reason.
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
    // used to be guarded by if (LIKELY_USE_CPUS_0)
    {
        NumaConfig splitNumaCfg = empty();

        NumaIndex splitNumaId = 0;
        for (const auto& cpus : numaCfg.nodes)
        {
            if (cpus.empty())
                continue;

            WORD lstGroupId = static_cast<WORD>(*cpus.begin() / WIN_PROCESSOR_GROUP_SIZE);

            for (CpuIndex cpuId : cpus)
            {
                const WORD groupId = static_cast<WORD>(cpuId / WIN_PROCESSOR_GROUP_SIZE);

                if (lstGroupId != groupId)
                {
                    lstGroupId = groupId;

                    ++splitNumaId;
                }

                splitNumaCfg.add_cpu_to_node(splitNumaId, cpuId);
            }

            ++splitNumaId;
        }

        numaCfg = std::move(splitNumaCfg);
    }
    #endif
#else
    // Fallback for unsupported systems
    for (CpuIndex cpuId = 0; cpuId < SYSTEM_THREAD_MAX; ++cpuId)
        numaCfg.add_cpu_to_node(NumaIndex{0}, cpuId);
#endif

    // Have to ensure no empty NUMA nodes persist
    numaCfg.remove_empty_numa_nodes();

    // If the user explicitly opts out from respecting the current process affinity
    // then it may be inconsistent with the current affinity (obviously),
    // so consider it custom.
    if (!respectProcessAffinity)
        numaCfg.customAffinity = true;

    return numaCfg;
}

std::optional<NumaConfig> NumaConfig::from_string(const std::string_view str) noexcept {
    NumaConfig numaCfg = empty();

    NumaIndex numaId = 0;

    for (const auto& cpuIdsStr : split(str, ":"))
    {
        const auto cpuIds = shortened_string_to_cpus(cpuIdsStr);

        if (cpuIds.empty())
            continue;

        for (const CpuIndex cpuId : cpuIds)
            if (!numaCfg.add_cpu_to_node(numaId, cpuId))
            {
                std::cerr << "NumaConfig parse error in segment '" << cpuIdsStr  //
                          << "': CPU " << cpuId << " rejected for NUMA node " << numaId
                          << std::endl;
                return std::nullopt;
            }

        ++numaId;
    }

    // Failed to parse any nodes
    if (numaId == 0)
        return std::nullopt;

    numaCfg.customAffinity = true;

    return numaCfg;
}

NumaConfig::NumaConfig(const CpuIndex maxCpuIdx, const bool customAff) noexcept :
    maxCpuId(maxCpuIdx),
    customAffinity(customAff) {}

NumaConfig::NumaConfig() noexcept :
    NumaConfig(0, false) {
    add_cpu_range_to_node(NumaIndex{0}, CpuIndex{0}, SYSTEM_THREAD_MAX - 1);
}

usize NumaConfig::nodes_size() const noexcept { return nodes.size(); }

CpuIndexVec& NumaConfig::node_cpus(const NumaIndex numaId) noexcept {
    assert(numaId < nodes_size());

    return nodes[numaId];
}
const CpuIndexVec& NumaConfig::node_cpus(const NumaIndex numaId) const noexcept {
    assert(numaId < nodes_size());

    return nodes[numaId];
}

bool NumaConfig::node_cpus_empty(const NumaIndex numaId) const noexcept {
    return node_cpus(numaId).empty();
}

usize NumaConfig::node_cpus_size(const NumaIndex numaId) const noexcept {
    return node_cpus(numaId).size();
}

CpuIndex NumaConfig::node_cpus_front(const NumaIndex numaId) const noexcept {
    assert(!node_cpus_empty(numaId));

    return node_cpus(numaId).front();
}

usize NumaConfig::cpus_size() const noexcept { return nodeByCpu.size(); }

bool NumaConfig::is_cpu_assigned(const CpuIndex cpuId) const noexcept {
    return nodeByCpu.find(cpuId) != nodeByCpu.end();
}

NumaIndex NumaConfig::node_by_cpu(const CpuIndex cpuId) const noexcept {
    const auto itr = nodeByCpu.find(cpuId);
    return itr != nodeByCpu.end() ? itr->second : 0;
}

bool NumaConfig::requires_memory_replication() const noexcept {
    return customAffinity || nodes_size() > 1;
}

std::string NumaConfig::to_string() const noexcept {
    std::string numaStr;
    // Reserve enough space for the CPU indices and separators
    usize cpuCount = 0;
    for (const auto& node : nodes)
        cpuCount += node.size();

    numaStr.reserve(6 * cpuCount);

    for (auto nodeItr = nodes.begin(); nodeItr != nodes.end(); ++nodeItr)
    {
        const auto& cpus = *nodeItr;
        assert(!cpus.empty());

        // Separate NUMA nodes with ':'
        if (nodeItr != nodes.begin())
            numaStr.push_back(':');

        for (auto cpusItr = cpus.begin(); cpusItr != cpus.end();)
        {
            const auto rangeItr = cpusItr;

            const CpuIndex rangeBeg = *cpusItr;
            CpuIndex       rangeEnd = rangeBeg;

            // Combine consecutive CPU indices into a range
            for (++cpusItr; cpusItr != cpus.end() && *cpusItr == rangeEnd + 1; ++cpusItr)
                ++rangeEnd;

            // Separate CPUs within a NUMA node with ','
            if (rangeItr != cpus.begin())
                numaStr.push_back(',');

            numaStr.append(std::to_string(rangeBeg));

            if (rangeBeg != rangeEnd)
            {
                numaStr.push_back('-');
                numaStr.append(std::to_string(rangeEnd));
            }
        }
    }

    return numaStr;
}

bool NumaConfig::suggests_binding_threads(const u16 threadCount) const noexcept {
    // If can reasonably determine that the threads can't be contained
    // by the OS within the first NUMA node then advise distributing
    // and binding threads. When the threads are not bound can only use
    // NUMA memory replicated objects from the first node, so when the OS
    // has to schedule on other nodes lose performance. Also suggest binding
    // if there's enough threads to distribute among nodes with minimal disparity.
    // Try to ignore small nodes, in particular the empty ones.

    // If the affinity set by the user does not match the affinity given by the OS
    // then binding is necessary to ensure the threads are running on correct processors.
    if (customAffinity)
        return true;

    // Obviously cannot distribute a single thread, so a single thread should never be bound
    if (threadCount <= 1)
        return false;

    // Only split if there is more than one node
    if (nodes_size() <= 1)
        return false;

    // Compute maximum node size
    const usize maxNodeSize =
      std::max_element(nodes.begin(), nodes.end(),  //
                       [](const auto& node1, const auto& node2) noexcept -> bool {
                           return node1.size() < node2.size();
                       })
        ->size();

    // Count nodes considered 'not-small' (size > 60% of maxNodeSize)
    const usize notSmallNodeCount =
      std::count_if(nodes.begin(), nodes.end(),  //
                    [maxNodeSize](const auto& node) noexcept -> bool {
                        constexpr double SmallNodeThreshold = 0.6;
                        // node considered 'not-small' if it exceeds threshold
                        return static_cast<double>(node.size()) / maxNodeSize > SmallNodeThreshold;
                    });

    // Split only if threadCount meets either threshold:
    //   - more than half of maxNodeSize, OR
    //   - at least four times notSmallNodeCount
    return threadCount >= std::min(maxNodeSize / 2 + 1, 4 * notSmallNodeCount);
}

std::vector<NumaIndex>
NumaConfig::distribute_threads_among_numa_nodes(const u16 threadCount) const noexcept {
    std::vector<NumaIndex> numaNodes;

    if (nodes_size() == 1)
    {
        // Special case for when there's no NUMA nodes
        // Doesn't buy much, but let's keep the default path simple
        numaNodes.resize(threadCount, NumaIndex{0});
    }
    else
    {
        std::vector<usize> occupation(nodes_size(), 0);

        for (u16 threadId = 0; threadId < threadCount; ++threadId)
        {
            NumaIndex bestNumaId = 0;

            double minNodeFill = std::numeric_limits<double>::max();

            for (usize numaId = 0; numaId < nodes_size(); ++numaId)
            {
                const double nodeFill =
                  double(occupation[numaId] + 1) / node_cpus_size(NumaIndex(numaId));
                // NOTE: Do want to perhaps fill the first available node up to 50% first before considering other nodes?
                //       Probably not, because it would interfere with running multiple instances.
                //       Basically shouldn't favor any particular node.
                if (minNodeFill > nodeFill)
                {
                    minNodeFill = nodeFill;
                    bestNumaId  = NumaIndex(numaId);
                }
            }

            numaNodes.emplace_back(bestNumaId);
            ++occupation[bestNumaId];
        }
    }

    return numaNodes;
}

NumaReplicatedAccessToken
NumaConfig::bind_current_thread_to_numa_node(const NumaIndex numaId) const noexcept {
    if (numaId >= nodes_size() || node_cpus_empty(numaId))
        std::exit(EXIT_FAILURE);

#if defined(_WIN64)
    // Requires Windows 11. No good way to set thread affinity spanning processor groups before that.
    HMODULE hModule = GetModuleHandle(KERNEL_MODULE_NAME);

    auto setThreadSelectedCpuSetMasks = SetThreadSelectedCpuSetMasks_(
      (void (*)()) GetProcAddress(hModule, "SetThreadSelectedCpuSetMasks"));

    // ALWAYS set affinity with the new API if available,
    // because there's no downsides, and forcibly keep it consistent with
    // the old API should need to use it. i.e. always keep this as a superset
    // of what set with SetThreadGroupAffinity.
    if (setThreadSelectedCpuSetMasks != nullptr)
    {
        // Only available on Windows 11 and Windows Server 2022 onwards
        const WORD procGroupCount =
          static_cast<WORD>(ceil_div(maxCpuId + 1, WIN_PROCESSOR_GROUP_SIZE));

        auto groupAffinities = std::make_unique<GROUP_AFFINITY[]>(procGroupCount);
        std::memset(groupAffinities.get(), 0, procGroupCount * sizeof(*groupAffinities.get()));

        for (WORD i = 0; i < procGroupCount; ++i)
            groupAffinities[i].Group = i;

        for (const CpuIndex cpuId : node_cpus(numaId))
        {
            const WORD groupId       = static_cast<WORD>(cpuId / WIN_PROCESSOR_GROUP_SIZE);
            const BYTE inProcGroupId = static_cast<BYTE>(cpuId % WIN_PROCESSOR_GROUP_SIZE);

            groupAffinities[groupId].Mask |= bit(u8(inProcGroupId));
        }

        if (setThreadSelectedCpuSetMasks(GetCurrentThread(), groupAffinities.get(), procGroupCount)
            == FALSE)
            std::exit(EXIT_FAILURE);

        // Yield this thread just to be sure it gets rescheduled.
        // This is defensive, allowed because this code is not performance critical.
        SwitchToThread();
    }

    // Sometimes need to force the old API, but do not use it unless necessary.
    if (setThreadSelectedCpuSetMasks == nullptr || LIKELY_USE_CPUS_0)
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

        // Use an ordered set so guaranteed to get the smallest cpu number here
        const WORD forcedGroupId =
          static_cast<WORD>(node_cpus_front(numaId) / WIN_PROCESSOR_GROUP_SIZE);

        groupAffinity.Group = forcedGroupId;

        for (const CpuIndex cpuId : node_cpus(numaId))
        {
            const WORD groupId       = static_cast<WORD>(cpuId / WIN_PROCESSOR_GROUP_SIZE);
            const WORD inProcGroupId = static_cast<WORD>(cpuId % WIN_PROCESSOR_GROUP_SIZE);
            // Skip processors that are not in the same processor group.
            // If everything was set up correctly this will never be an issue,
            // but have to account for bad NUMA node specification.
            if (groupId == forcedGroupId)
                groupAffinity.Mask |= bit(u8(inProcGroupId));
        }

        if (SetThreadGroupAffinity(GetCurrentThread(), &groupAffinity, nullptr) == FALSE)
            std::exit(EXIT_FAILURE);

        // Yield this thread just to be sure it gets rescheduled.
        // This is defensive, allowed because this code is not performance critical.
        SwitchToThread();
    }

#elif defined(USE_UNIX_NUMA)
    cpu_set_t* const cpuMask = CPU_ALLOC(maxCpuId + 1);

    if (cpuMask == nullptr)
        std::exit(EXIT_FAILURE);

    const usize maskSize = CPU_ALLOC_SIZE(maxCpuId + 1);

    CPU_ZERO_S(maskSize, cpuMask);

    for (const CpuIndex cpuId : node_cpus(numaId))
        CPU_SET_S(cpuId, maskSize, cpuMask);

    if (::sched_setaffinity(0, maskSize, cpuMask) != 0)
    {
        DEBUG_LOG("::sched_setaffinity() failed");

        CPU_FREE(cpuMask);
        std::exit(EXIT_FAILURE);
    }

    CPU_FREE(cpuMask);

    // Yield this thread just to be sure it gets rescheduled.
    // This is defensive, allowed because this code is not performance critical.
    ::sched_yield();
#endif

    return NumaReplicatedAccessToken(numaId);
}

NumaConfig NumaConfig::from_l3_domain(const std::vector<L3Domain> l3Domains,
                                      const usize                 bundleSize) noexcept {
    assert(!l3Domains.empty());

    NumaConfig numaCfg = empty();

    std::unordered_map<NumaIndex, std::vector<L3Domain>> numaL3Domains;

    for (auto& l3Domain : l3Domains)
        numaL3Domains[l3Domain.sysNumaId].push_back(std::move(l3Domain));

    NumaIndex numaId = 0;

    for (auto& [_, ds] : numaL3Domains)
    {
        bool changed;
        // Scan through pairs and merge them. With roughly equal L3 sizes, should give a decent distribution
        do
        {
            changed = false;

            for (usize i = 0; i + 1 < ds.size(); ++i)
                if (ds[i].cpus.size() + ds[i + 1].cpus.size() <= bundleSize)
                {
                    ds[i].cpus.merge(ds[i + 1].cpus);

                    ds.erase(ds.begin() + i + 1);

                    changed = true;
                }

            // ds.size() has decreased if changed is true, so this loop will terminate
        } while (changed);

        for (const auto& [__, cpus] : ds)
        {
            for (CpuIndex cpuId : cpus)
                if (!numaCfg.add_cpu_to_node(numaId, cpuId))
                {
                    std::cerr << "NumaConfig l3 domain error: CPU " << cpuId
                              << " rejected for NUMA node " << numaId << std::endl;
                }

            ++numaId;
        }
    }

    return numaCfg;
}

void NumaConfig::resize_numa_node(const usize newNumaId) noexcept {
    if (nodes_size() <= newNumaId)
        nodes.resize(newNumaId + 1);  // default-construct missing elements
}

void NumaConfig::add_numa_node_cpu(const NumaIndex numaId, const CpuIndex cpuId) noexcept {
    // insert/update mapping
    nodeByCpu[cpuId] = numaId;
    // track max CPU ID
    maxCpuId = std::max(cpuId, maxCpuId);
}

void NumaConfig::add_numa_node(const NumaIndex numaId, const CpuIndex cpuId) noexcept {
    auto& cpus = node_cpus(numaId);

    // Keep CPU indices sorted and unique.
    if (cpus.empty() || cpus.back() < cpuId)
        cpus.push_back(cpuId);
    else
    {
        const auto cpusItr = std::lower_bound(cpus.begin(), cpus.end(), cpuId);
        assert(cpusItr == cpus.end() || *cpusItr != cpuId);
        cpus.insert(cpusItr, cpuId);
    }

    add_numa_node_cpu(numaId, cpuId);
}

bool NumaConfig::add_cpu_to_node(const NumaIndex numaId, const CpuIndex cpuId) noexcept {

    if (is_cpu_assigned(cpuId))
        return false;

    resize_numa_node(numaId);

    add_numa_node(numaId, cpuId);

    return true;
}

bool NumaConfig::add_cpu_range_to_node(const NumaIndex numaId,
                                       const CpuIndex  cpuIdBeg,
                                       const CpuIndex  cpuIdEnd) noexcept {

    for (auto cpuId = cpuIdBeg; cpuId <= cpuIdEnd; ++cpuId)
        if (is_cpu_assigned(cpuId))
            return false;

    resize_numa_node(numaId);

    for (auto cpuId = cpuIdBeg; cpuId <= cpuIdEnd; ++cpuId)
        add_numa_node(numaId, cpuId);

    return true;
}

void NumaConfig::remove_empty_numa_nodes() noexcept {

    bool hasEmpty = false;

    for (const auto& node : nodes)
        if (node.empty())
        {
            hasEmpty = true;
            break;
        }

    // 1. Nothing removed -> skip everything
    if (!hasEmpty)
        return;

    // 2. Remove empty nodes
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                               [](const auto& node) noexcept { return node.empty(); }),
                nodes.end());

    // 3. Rebuild mapping structures efficiently
    nodeByCpu.clear();
    maxCpuId = 0;

    for (usize numaId = 0; numaId < nodes_size(); ++numaId)
        for (const CpuIndex cpuId : node_cpus(NumaIndex(numaId)))
            add_numa_node_cpu(NumaIndex(numaId), cpuId);
}


NumaReplicationContext::NumaReplicationContext(NumaConfig&& numaCfg) noexcept :
    numaConfig(std::move(numaCfg)) {}

NumaReplicationContext::~NumaReplicationContext() noexcept {
    // The context must outlive all attached replicated objects.
    if (!replicatedSet.empty())
        std::exit(EXIT_FAILURE);
}

void NumaReplicationContext::attach(BaseNumaReplicated* const numaRep) noexcept {
    assert(replicatedSet.find(numaRep) == replicatedSet.end());

    replicatedSet.insert(numaRep);
}

void NumaReplicationContext::detach(BaseNumaReplicated* const numaRep) noexcept {
    assert(replicatedSet.find(numaRep) != replicatedSet.end());

    replicatedSet.erase(numaRep);
}

void NumaReplicationContext::move(BaseNumaReplicated* const oldNumaRep,
                                  BaseNumaReplicated* const newNumaRep) noexcept {
    assert(replicatedSet.find(oldNumaRep) != replicatedSet.end());
    assert(replicatedSet.find(newNumaRep) == replicatedSet.end());

    replicatedSet.erase(oldNumaRep);
    replicatedSet.insert(newNumaRep);
}

void NumaReplicationContext::set_numa_config(NumaConfig&& numaCfg) noexcept {
    numaConfig = std::move(numaCfg);

    for (auto* numaRep : replicatedSet)
        numaRep->on_numa_config_changed();
}

const NumaConfig& NumaReplicationContext::numa_config() const noexcept { return numaConfig; }


BaseNumaReplicated::BaseNumaReplicated(NumaReplicationContext& numaCtx) noexcept :
    numaContext(&numaCtx) {
    attach_context();
}

BaseNumaReplicated::BaseNumaReplicated(BaseNumaReplicated&& baseNumaRep) noexcept :
    numaContext(std::exchange(baseNumaRep.numaContext, nullptr)) {
    move_context(baseNumaRep);
}

BaseNumaReplicated& BaseNumaReplicated::operator=(BaseNumaReplicated&& baseNumaRep) noexcept {
    if (this == &baseNumaRep)
        return *this;

    // Remove this object from its current context.
    detach_context();

    // Transfer the source context and clear the source context pointer.
    numaContext = std::exchange(baseNumaRep.numaContext, nullptr);

    // Replace the source address with this object's address in the context.
    move_context(baseNumaRep);

    return *this;
}

BaseNumaReplicated::~BaseNumaReplicated() noexcept { detach_context(); }

const NumaConfig& BaseNumaReplicated::numa_config() const noexcept {
    static const NumaConfig emptyCfg = NumaConfig::empty();

    return numaContext != nullptr ? numaContext->numa_config() : emptyCfg;
}

void BaseNumaReplicated::attach_context() noexcept {
    if (numaContext != nullptr)
        numaContext->attach(this);
}

void BaseNumaReplicated::detach_context() noexcept {
    if (numaContext != nullptr)
        numaContext->detach(this);

    numaContext = nullptr;
}

void BaseNumaReplicated::move_context(BaseNumaReplicated& baseNumaRep) noexcept {
    if (numaContext != nullptr)
        numaContext->move(&baseNumaRep, this);
}

}  // namespace DON
