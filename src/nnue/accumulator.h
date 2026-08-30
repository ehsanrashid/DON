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

// Class for difference calculation of NNUE evaluation function

#ifndef NNUE_ACCUMULATOR_H_INCLUDED
#define NNUE_ACCUMULATOR_H_INCLUDED

#include <array>
#include <cstddef>
#include <cstring>
#include <utility>

#include "../misc.h"
#include "../types.h"
#include "ntypes.h"

namespace DON {

class Position;

namespace NNUE {

class FeatureTransformer;

// Stores the accumulated affine-transformation results for HalfKA_hm and FullThreats.
struct alignas(CACHE_LINE_SIZE) BaseAccumulator {
   public:
    Array<BiasType, COLOR_NB, L1>                accumulation;
    Array<PSQTWeightType, COLOR_NB, PSQTBuckets> psqtAccumulation;
    Array<bool, COLOR_NB>                        computed{};
};

static_assert(sizeof(BaseAccumulator) % CACHE_LINE_SIZE == 0);

struct Accumulator final: public BaseAccumulator {
   public:
    void set(DirtyBoard&& db) noexcept {
        dirtyBoard = std::move(db);
        computed.fill(false);
    }

    DirtyBoard dirtyBoard;
};

static_assert(alignof(Accumulator) == CACHE_LINE_SIZE);

// AccumulatorCache provides per-thread accumulator cache,
// where each cache contains multiple entries for each of the possible king squares.
// When the accumulator needs to be refreshed, the cached entry is used to more
// efficiently update the accumulator, instead of rebuilding it from scratch.
// This idea, was first described by Luecx (author of Koivisto) and
// is commonly referred to as "Finny Tables".
struct AccumulatorCache final {
   public:
    struct alignas(CACHE_LINE_SIZE) Entry final {
       public:
        // To initialize a refresh entry, set all its bitboards empty,
        // so put the biases in the accumulation, without any weights on top
        void init(const Array<BiasType, L1>& biases) noexcept {
            // Initialize accumulation with given biases
            accumulation          = biases;
            constexpr auto offset = offsetof(Entry, psqtAccumulation);
            static_assert(offset <= sizeof(Entry), "offset exceeds object size");
            std::memset(reinterpret_cast<uchar*>(this) + offset, 0, sizeof(*this) - offset);
        }

        Array<BiasType, L1>                accumulation;
        Array<PSQTWeightType, PSQTBuckets> psqtAccumulation;
        PieceMap                           pieceMap;
        Bitboard                           piecesBB;
    };

    template<typename Network>
    explicit AccumulatorCache(const Network& network) noexcept {
        init(network);
    }

    template<typename Network>
    void init(const Network& network) noexcept {
        for (auto& sqEntries : entries)
            for (auto& entry : sqEntries)
                entry.init(network.featureTransformer.biases);
    }

    const Array<Entry, COLOR_NB>& operator[](const Square s) const noexcept { return entries[s]; }
    Array<Entry, COLOR_NB>&       operator[](const Square s) noexcept { return entries[s]; }

   private:
    Array<Entry, SQUARE_NB, COLOR_NB> entries;
};

struct AccumulatorStack final {
   public:
    void reset() noexcept;
    void push(DirtyBoard&& db) noexcept;
    void pop() noexcept;

    [[nodiscard]] usize size() const noexcept { return size_; }

    [[nodiscard]] auto&       top() noexcept { return accumulators[size() - 1]; }
    [[nodiscard]] const auto& top() const noexcept { return accumulators[size() - 1]; }


    void evaluate(const Position&           pos,
                  const FeatureTransformer& featureTransformer,
                  AccumulatorCache&         accCache) noexcept;

   private:
    void evaluate(Color                     perspective,
                  const Position&           pos,
                  const FeatureTransformer& featureTransformer,
                  AccumulatorCache&         accCache) noexcept;

    [[nodiscard]] usize find_last_usable_index(Color perspective) const noexcept;

    void update_incremental_forward(Color                     perspective,
                                    const Position&           pos,
                                    const FeatureTransformer& featureTransformer,
                                    usize                     beg) noexcept;

    void update_incremental_backward(Color                     perspective,
                                     const Position&           pos,
                                     const FeatureTransformer& featureTransformer,
                                     usize                     end) noexcept;

    static constexpr usize Size = PLY_MAX + 1;

    Array<Accumulator, Size> accumulators;
    usize                    size_ = 1;
};

}  // namespace NNUE
}  // namespace DON

#endif  // NNUE_ACCUMULATOR_H_INCLUDED
