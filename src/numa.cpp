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

CpuIndexVec shortened_string_to_indices(std::string_view str) noexcept {
    CpuIndexVec indices;

    if (is_whitespace(str))
        return indices;

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
                indices.emplace_back(CpuIndex(*cpuId));
        }
        break;
        case 2 : {
            // Limit expansion to 1M CPU IDs
            constexpr usize MaxIndices = 64 * KB;

            if (indices.size() >= MaxIndices)
                break;

            const auto begId = str_to_usize(parts[0]);
            const auto endId = str_to_usize(parts[1]);

            if (begId && endId && *begId <= *endId && *endId - *begId < MaxIndices - indices.size()
                && *endId <= std::numeric_limits<CpuIndex>::max())
            {
                const auto begCpuId = static_cast<CpuIndex>(*begId);
                const auto endCpuId = static_cast<CpuIndex>(*endId);

                for (CpuIndex cpuId = begCpuId;; ++cpuId)
                {
                    indices.emplace_back(cpuId);
                    if (cpuId == endCpuId)
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

    return indices;
}

BaseNumaReplicated::BaseNumaReplicated(NumaReplicationContext& numaCtx) noexcept :
    numaContext(&numaCtx) {
    if (numaContext != nullptr)
        numaContext->attach(this);
}

void BaseNumaReplicated::detach_context() noexcept {
    if (numaContext != nullptr)
    {
        numaContext->detach(this);
        numaContext = nullptr;
    }
}

BaseNumaReplicated::BaseNumaReplicated(BaseNumaReplicated&& baseNumaRep) noexcept :
    numaContext(std::exchange(baseNumaRep.numaContext, nullptr)) {
    if (numaContext != nullptr)
        numaContext->move_attached(&baseNumaRep, this);
}

BaseNumaReplicated& BaseNumaReplicated::operator=(BaseNumaReplicated&& baseNumaRep) noexcept {
    if (this == &baseNumaRep)
        return *this;

    detach_context();  // cleanup existing context

    numaContext = std::exchange(baseNumaRep.numaContext, nullptr);

    if (numaContext != nullptr)
        numaContext->move_attached(&baseNumaRep, this);

    return *this;
}

BaseNumaReplicated::~BaseNumaReplicated() noexcept { detach_context(); }

const NumaConfig& BaseNumaReplicated::numa_config() const noexcept {
    static const NumaConfig EmptyCfg = NumaConfig::empty();

    return numaContext != nullptr ? numaContext->numa_config() : EmptyCfg;
}


}  // namespace DON
