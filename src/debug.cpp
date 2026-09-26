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

#include "debug.h"

#include <array>
#include <atomic>
#include <cmath>
#include <limits>

namespace DON {

namespace Debug {

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

class ExtremeInfo final: public Info<3> {
   public:
    ExtremeInfo() noexcept :
        Info() {
        data[1].store(std::numeric_limits<i64>::max(), std::memory_order_relaxed);
        data[2].store(std::numeric_limits<i64>::min(), std::memory_order_relaxed);
    }
};


constexpr usize SLOT_MAX = 64;

Array<Info<2>, SLOT_MAX>     hit;
Array<MinInfo, SLOT_MAX>     min;
Array<MaxInfo, SLOT_MAX>     max;
Array<ExtremeInfo, SLOT_MAX> extreme;
Array<Info<2>, SLOT_MAX>     mean;
Array<Info<3>, SLOT_MAX>     stdev;
Array<Info<6>, SLOT_MAX>     correl;

}  // namespace

void clear() noexcept {
    hit.fill({});
    min.fill({});
    max.fill({});
    extreme.fill({});
    mean.fill({});
    stdev.fill({});
    correl.fill({});
}

void hit_on(const bool cond, const usize slot) noexcept {
    assert(slot < hit.size());
    if (slot >= hit.size())
        return;

    auto& info = hit[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    if (cond)
        info[1].fetch_add(1, std::memory_order_relaxed);
}

void min_of(const i64 value, const usize slot) noexcept {
    assert(slot < min.size());
    if (slot >= min.size())
        return;

    auto& info = min[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    auto& info_1   = info[1];
    i64   minValue = info_1.load(std::memory_order_relaxed);
    while (minValue > value
           && !info_1.compare_exchange_weak(minValue, value,  //
                                            std::memory_order_relaxed, std::memory_order_relaxed))
    {}
}

void max_of(const i64 value, const usize slot) noexcept {
    assert(slot < max.size());
    if (slot >= max.size())
        return;

    auto& info = max[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    auto& info_1   = info[1];
    i64   maxValue = info_1.load(std::memory_order_relaxed);
    while (maxValue < value
           && !info_1.compare_exchange_weak(maxValue, value,  //
                                            std::memory_order_relaxed, std::memory_order_relaxed))
    {}
}

void extreme_of(const i64 value, const usize slot) noexcept {
    assert(slot < extreme.size());
    if (slot >= extreme.size())
        return;

    auto& info = extreme[slot];

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
    assert(slot < mean.size());
    if (slot >= mean.size())
        return;

    auto& info = mean[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    info[1].fetch_add(value, std::memory_order_relaxed);
}

void stdev_of(const i64 value, const usize slot) noexcept {
    assert(slot < stdev.size());
    if (slot >= stdev.size())
        return;

    auto& info = stdev[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    info[1].fetch_add(value, std::memory_order_relaxed);
    info[2].fetch_add(value * value, std::memory_order_relaxed);
}

void correl_of(const i64 value1, const i64 value2, const usize slot) noexcept {
    assert(slot < correl.size());
    if (slot >= correl.size())
        return;

    auto& info = correl[slot];

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

    for (usize i = 0; i < hit.size(); ++i)
    {
        const auto& info = hit[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        const i64 hits = info[1].load(std::memory_order_relaxed);

        std::cerr << "Hit #" << i << ": Count=" << n  //
                  << " Hits=" << hits                 //
                  << " Hit Rate (%)=" << 100 * avg(hits) << std::endl;
    }

    for (usize i = 0; i < min.size(); ++i)
    {
        const auto& info = min[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        const i64 minValue = info[1].load(std::memory_order_relaxed);

        std::cerr << "Min #" << i << ": Count=" << n  //
                  << " Min=" << minValue << std::endl;
    }

    for (usize i = 0; i < max.size(); ++i)
    {
        const auto& info = max[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        const i64 maxValue = info[1].load(std::memory_order_relaxed);

        std::cerr << "Max #" << i << ": Count=" << n  //
                  << " Max=" << maxValue << std::endl;
    }

    for (usize i = 0; i < extreme.size(); ++i)
    {
        const auto& info = extreme[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        const i64 minValue = info[1].load(std::memory_order_relaxed);
        const i64 maxValue = info[2].load(std::memory_order_relaxed);

        std::cerr << "Extreme #" << i << ": Count=" << n  //
                  << " Min=" << minValue                  //
                  << " Max=" << maxValue << std::endl;
    }

    for (usize i = 0; i < mean.size(); ++i)
    {
        const auto& info = mean[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        const i64 sum = info[1].load(std::memory_order_relaxed);

        std::cerr << "Mean #" << i << ": Count=" << n  //
                  << " Sum=" << sum                    //
                  << " Mean=" << avg(sum) << std::endl;
    }

    for (usize i = 0; i < stdev.size(); ++i)
    {
        const auto& info = stdev[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        const i64 sum   = info[1].load(std::memory_order_relaxed);
        const i64 sumSq = info[2].load(std::memory_order_relaxed);

        const auto r = std::sqrt(avg(sumSq) - sqr(avg(sum)));

        std::cerr << "Stdev #" << i << ": Count=" << n  //
                  << " Stdev=" << r << std::endl;
    }

    for (usize i = 0; i < correl.size(); ++i)
    {
        const auto& info = correl[i];

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

}  // namespace Debug

}  // namespace DON
