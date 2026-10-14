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

#include "../utility/debug.h"

#include <atomic>
#include <cassert>
#include <cmath>  // sqrt()
#include <iostream>
#include <limits>   // numeric_limits<>
#include <utility>  // as_const()

#include "../utility/types.h"

namespace DON::Debug {

namespace {

template<usize Size>
class Info {
   public:
    Info() noexcept = default;

    Info(const Info& info) noexcept { copy(info); }
    Info& operator=(const Info& info) noexcept {
        if (this == &info)
            return *this;

        copy(info);

        return *this;
    }

    Info(Info&&) noexcept            = delete;
    Info& operator=(Info&&) noexcept = delete;

    [[nodiscard]] decltype(auto) operator[](usize index) const noexcept {
        assert(index < Size && "Index out of bounds");
        return data[index];
    }
    [[nodiscard]] decltype(auto) operator[](usize index) noexcept {
        assert(index < Size && "Index out of bounds");
        return data[index];
    }

    void reset() noexcept {
        for (usize i = 0; i < Size; ++i)
            data[i].store(0, std::memory_order_relaxed);
    }

   protected:
    Array<std::atomic<i64>, Size> data{0};

   private:
    void copy(const Info& info) noexcept {
        for (usize i = 0; i < Size; ++i)
            data[i].store(info.data[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
};

using HitInfo = Info<2>;

class MinInfo final: public Info<2> {
   public:
    MinInfo() noexcept :
        Info() {
        data[1].store(std::numeric_limits<i64>::max(), std::memory_order_relaxed);
    }
};

class MaxInfo final: public Info<2> {
   public:
    MaxInfo() noexcept :
        Info() {
        data[1].store(std::numeric_limits<i64>::min(), std::memory_order_relaxed);
    }
};

class MinMaxInfo final: public Info<3> {
   public:
    MinMaxInfo() noexcept :
        Info() {
        data[1].store(std::numeric_limits<i64>::max(), std::memory_order_relaxed);
        data[2].store(std::numeric_limits<i64>::min(), std::memory_order_relaxed);
    }
};

using MeanInfo = Info<2>;

using StdevInfo = Info<3>;

using CorrelInfo = Info<6>;

constexpr usize SLOT_MAX = 64;

// clang-format off
Array<HitInfo   , SLOT_MAX> Hit;
Array<MinInfo   , SLOT_MAX> Min;
Array<MaxInfo   , SLOT_MAX> Max;
Array<MinMaxInfo, SLOT_MAX> MinMax;
Array<MeanInfo  , SLOT_MAX> Mean;
Array<StdevInfo , SLOT_MAX> Stdev;
Array<CorrelInfo, SLOT_MAX> Correl;
// clang-format on

}  // namespace

void hit_on(const bool cond, const usize slot) noexcept {
    assert(slot < Hit.size());
    if (slot >= Hit.size())
        return;

    auto& info = Hit[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    if (cond)
        info[1].fetch_add(1, std::memory_order_relaxed);
}

void min_of(const i64 value, const usize slot) noexcept {
    assert(slot < Min.size());
    if (slot >= Min.size())
        return;

    auto& info = Min[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    auto& info_1   = info[1];
    i64   minValue = info_1.load(std::memory_order_relaxed);
    while (minValue > value
           && !info_1.compare_exchange_weak(minValue, value,  //
                                            std::memory_order_relaxed, std::memory_order_relaxed))
    {}
}

void max_of(const i64 value, const usize slot) noexcept {
    assert(slot < Max.size());
    if (slot >= Max.size())
        return;

    auto& info = Max[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    auto& info_1   = info[1];
    i64   maxValue = info_1.load(std::memory_order_relaxed);
    while (maxValue < value
           && !info_1.compare_exchange_weak(maxValue, value,  //
                                            std::memory_order_relaxed, std::memory_order_relaxed))
    {}
}

void min_max_of(const i64 value, const usize slot) noexcept {
    assert(slot < MinMax.size());
    if (slot >= MinMax.size())
        return;

    auto& info = MinMax[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    auto& info_1   = info[1];
    i64   minValue = info_1.load(std::memory_order_relaxed);
    while (minValue > value
           && !info_1.compare_exchange_weak(minValue, value,  //
                                            std::memory_order_relaxed, std::memory_order_relaxed))
    {}
    auto& info_2   = info[2];
    i64   maxValue = info_2.load(std::memory_order_relaxed);
    while (maxValue < value
           && !info_2.compare_exchange_weak(maxValue, value,  //
                                            std::memory_order_relaxed, std::memory_order_relaxed))
    {}
}

void mean_of(const i64 value, const usize slot) noexcept {
    assert(slot < Mean.size());
    if (slot >= Mean.size())
        return;

    auto& info = Mean[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    info[1].fetch_add(value, std::memory_order_relaxed);
}

void stdev_of(const i64 value, const usize slot) noexcept {
    assert(slot < Stdev.size());
    if (slot >= Stdev.size())
        return;

    auto& info = Stdev[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    info[1].fetch_add(value, std::memory_order_relaxed);
    info[2].fetch_add(value * value, std::memory_order_relaxed);
}

void correl_of(const i64 value1, const i64 value2, const usize slot) noexcept {
    assert(slot < Correl.size());
    if (slot >= Correl.size())
        return;

    auto& info = Correl[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    info[1].fetch_add(value1, std::memory_order_relaxed);
    info[2].fetch_add(value1 * value1, std::memory_order_relaxed);
    info[3].fetch_add(value2, std::memory_order_relaxed);
    info[4].fetch_add(value2 * value2, std::memory_order_relaxed);
    info[5].fetch_add(value1 * value2, std::memory_order_relaxed);
}

void print() noexcept {

    i64        n;
    const auto avg = [&n = std::as_const(n)](const i64 x) noexcept { return double(x) / n; };

    for (usize i = 0; i < Hit.size(); ++i)
    {
        const auto& info = Hit[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        const i64 hit = info[1].load(std::memory_order_relaxed);

        std::cerr << "Hit #" << i << ": Count=" << n  //
                  << " Hit=" << hit                   //
                  << " Hit Rate (%)=" << 100 * avg(hit) << std::endl;
    }

    for (usize i = 0; i < Min.size(); ++i)
    {
        const auto& info = Min[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        const i64 minValue = info[1].load(std::memory_order_relaxed);

        std::cerr << "Min #" << i << ": Count=" << n  //
                  << " Min=" << minValue << std::endl;
    }

    for (usize i = 0; i < Max.size(); ++i)
    {
        const auto& info = Max[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        const i64 maxValue = info[1].load(std::memory_order_relaxed);

        std::cerr << "Max #" << i << ": Count=" << n  //
                  << " Max=" << maxValue << std::endl;
    }

    for (usize i = 0; i < MinMax.size(); ++i)
    {
        const auto& info = MinMax[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        const i64 minValue = info[1].load(std::memory_order_relaxed);
        const i64 maxValue = info[2].load(std::memory_order_relaxed);

        std::cerr << "Extreme #" << i << ": Count=" << n  //
                  << " Min=" << minValue                  //
                  << " Max=" << maxValue << std::endl;
    }

    for (usize i = 0; i < Mean.size(); ++i)
    {
        const auto& info = Mean[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        const i64 sum = info[1].load(std::memory_order_relaxed);

        std::cerr << "Mean #" << i << ": Count=" << n  //
                  << " Sum=" << sum                    //
                  << " Mean=" << avg(sum) << std::endl;
    }

    for (usize i = 0; i < Stdev.size(); ++i)
    {
        const auto& info = Stdev[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        const i64 sum   = info[1].load(std::memory_order_relaxed);
        const i64 sumSq = info[2].load(std::memory_order_relaxed);

        const auto r = std::sqrt(avg(sumSq) - sqr(avg(sum)));

        std::cerr << "Stdev #" << i << ": Count=" << n  //
                  << " Stdev=" << r << std::endl;
    }

    for (usize i = 0; i < Correl.size(); ++i)
    {
        const auto& info = Correl[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        const i64 sum_v1   = info[1].load(std::memory_order_relaxed);
        const i64 sumSq_v1 = info[2].load(std::memory_order_relaxed);
        const i64 sum_v2   = info[3].load(std::memory_order_relaxed);
        const i64 sumSq_v2 = info[4].load(std::memory_order_relaxed);
        const i64 sum_v1v2 = info[5].load(std::memory_order_relaxed);

        const auto r = (avg(sum_v1v2) - avg(sum_v1) * avg(sum_v2))   //
                     / (std::sqrt(avg(sumSq_v1) - sqr(avg(sum_v1)))  //
                        * std::sqrt(avg(sumSq_v2) - sqr(avg(sum_v2))));

        std::cerr << "Correl #" << i << ": Count=" << n  //
                  << " Correl=" << r << std::endl;
    }
}

void reset() noexcept {
    // clang-format off
    Hit   .fill({});
    Min   .fill({});
    Max   .fill({});
    MinMax.fill({});
    Mean  .fill({});
    Stdev .fill({});
    Correl.fill({});
    // clang-format on
}

}  // namespace DON::Debug
