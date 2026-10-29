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

#include "../types.h"
#include "../nnue/types.h"
#include "../nnue/features/k32h3.h"
#include "../utility/types.h"

namespace DON {

class Position;

namespace NNUE {

class FeatureTransformer;

// Stores the accumulated affine-transformation results for HalfKAHm and FullThreats.
struct alignas(CACHE_LINE_SIZE) BaseAccumulator {
   public:
    Array<Bias, COLOR_NB, L1> accumulation;
    Array<bool, COLOR_NB>     computed{};
};

static_assert(sizeof(BaseAccumulator) % CACHE_LINE_SIZE == 0);

struct Accumulator final: public BaseAccumulator {
   public:
    void clear() noexcept {
        computed.fill(false);
        dirties     = {};
        beforeBlock = {};
    }

    Dirties                dirties;
    Features::K32H3::Block beforeBlock;
};

static_assert(alignof(Accumulator) == CACHE_LINE_SIZE);

// AccumulatorCache provides per-thread accumulator cache,
// where each cache contains multiple entries for each of the possible king squares.
// When the accumulator needs to be refreshed, the cached entry is used to more
// efficiently update the accumulator, instead of rebuilding it from scratch.
// This idea was first described by Luecx (author of Koivisto) and
// is commonly referred to as "Finny Tables".
struct AccumulatorCache final {
   public:
    struct alignas(CACHE_LINE_SIZE) Entry final {
       public:
        // To initialize a refresh entry, set all its bitboards empty,
        // so put the biases in the accumulation, without any weights on top.
        void init(const Array<Bias, L1>& biases) noexcept;

        Array<Bias, L1> accumulation;
        PieceMap        pieceMap;
        Bitboard        piecesBB;
    };

    template<typename Network>
    explicit AccumulatorCache(const Network& network) noexcept {
        init(network);
    }

    template<typename Network>
    void init(const Network& network) noexcept {
        for (auto& qEntries : entries)
            for (auto& sqEntry : qEntries)
                for (auto& entry : sqEntry)
                    entry.init(network.featureTransformer.biases);
    }

    auto&       operator[](const u8 psqBlock) noexcept { return entries[psqBlock]; }
    const auto& operator[](const u8 psqBlock) const noexcept { return entries[psqBlock]; }

   private:
    Array<Entry, Features::K32H3::NumBlocks, SQUARE_NB, COLOR_NB> entries;
};

struct AccumulatorStack final {
   public:
    AccumulatorStack() noexcept { reset(); }

    void reset() noexcept;

    Dirties& push(const Position& pos) noexcept;

    void pop() noexcept;

    [[nodiscard]] usize size() const noexcept;

    [[nodiscard]] Accumulator&       top() noexcept;
    [[nodiscard]] const Accumulator& top() const noexcept;


    void evaluate(const Position&           pos,
                  const FeatureTransformer& featureTransformer,
                  AccumulatorCache&         accCache) noexcept;

   private:
    void push_and_clear() noexcept;

    void evaluate(Color                     perspective,
                  const Position&           pos,
                  const FeatureTransformer& featureTransformer,
                  AccumulatorCache&         accCache,
                  usize                     lastUsableIdx) noexcept;

    // Find the earliest usable accumulator, this can either be a computed accumulator or the accumulator
    // state just before a change that requires full refresh.
    [[nodiscard]] usize find_last_usable_index(Color           perspective,
                                               const Position& pos) const noexcept;

    void update_incremental_forward(Color                     perspective,
                                    const Position&           pos,
                                    const FeatureTransformer& featureTransformer,
                                    usize                     beg) noexcept;

    void update_incremental_backward(Color                     perspective,
                                     const Position&           pos,
                                     const FeatureTransformer& featureTransformer,
                                     usize                     end) noexcept;

    void update_incremental_forward_both(const Position&           pos,
                                         const FeatureTransformer& featureTransformer,
                                         usize                     wBeg,
                                         usize                     bBeg) noexcept;

    static constexpr usize Size = PLY_MAX + 1;

    Array<Accumulator, Size> accumulators;
    usize                    size_;
};

}  // namespace NNUE
}  // namespace DON

#endif  // NNUE_ACCUMULATOR_H_INCLUDED
