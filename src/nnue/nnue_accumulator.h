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

// Class for difference calculation of NNUE evaluation function

#ifndef NNUE_ACCUMULATOR_H_INCLUDED
#define NNUE_ACCUMULATOR_H_INCLUDED

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../types.h"
#include "nnue_architecture.h"
#include "nnue_common.h"

namespace DON {

class Position;

namespace NNUE {

struct Networks;

using BiasType       = std::int16_t;
using PSQTWeightType = std::int32_t;
using IndexType      = std::uint32_t;

// Accumulator holds the result of affine transformation of input features
template<IndexType Size>
struct alignas(CACHE_LINE_SIZE) Accumulator final {
    BiasType                   accumulation[COLOR_NB][Size];
    PSQTWeightType             psqtAccumulation[COLOR_NB][PSQTBuckets];
    std::array<bool, COLOR_NB> computed;
};

using BigAccumulator   = Accumulator<BigTransformedFeatureDimensions>;
using SmallAccumulator = Accumulator<SmallTransformedFeatureDimensions>;

template<IndexType Size>
struct alignas(CACHE_LINE_SIZE) Cache final {

    struct alignas(CACHE_LINE_SIZE) Entry final {

        BiasType       accumulation[Size];
        PSQTWeightType psqtAccumulation[PSQTBuckets];
        Bitboard       colorBB[COLOR_NB];
        Bitboard       typeBB[PIECE_TYPE_NB];

        // To initialize a refresh entry, set all its bitboards empty,
        // so put the biases in the accumulation, without any weights on top
        void init(const BiasType* biases) noexcept {

            std::memcpy(accumulation, biases, sizeof(accumulation));
            auto offset = offsetof(Entry, psqtAccumulation);
            std::memset((std::uint8_t*) this + offset, 0, sizeof(Entry) - offset);
        }
    };

    template<typename Network>
    void init(const Network& network) noexcept {

        for (auto& subEntry : entries)
            for (auto& entry : subEntry)
                entry.init(network.featureTransformer->biases);
    }

    auto& operator[](Square s) noexcept { return entries[s]; }

    std::array<std::array<Entry, COLOR_NB>, SQUARE_NB> entries;
};

using BigCache   = Cache<BigTransformedFeatureDimensions>;
using SmallCache = Cache<SmallTransformedFeatureDimensions>;

struct AccumulatorState final {
    DirtyPiece       dirtyPiece;
    BigAccumulator   big;
    SmallAccumulator small;

    void reset(const DirtyPiece& dp) noexcept;
};

// AccumulatorCaches provides per-thread accumulator caches,
// where each cache contains multiple entries for each of the possible king squares.
// When the accumulator needs to be refreshed, the cached entry is used to more
// efficiently update the accumulator, instead of rebuilding it from scratch.
// This idea, was first described by Luecx (author of Koivisto) and
// is commonly referred to as "Finny Tables".
struct AccumulatorCaches final {

    explicit AccumulatorCaches(const Networks& networks) noexcept { init(networks); }

    void init(const Networks& networks) noexcept;

    BigCache   big;
    SmallCache small;
};

template<IndexType                                 TransformedFeatureDimensions,
         Accumulator<TransformedFeatureDimensions> AccumulatorState::*accPtr>
class FeatureTransformer;

class AccumulatorStack final {
   public:
    AccumulatorStack() :
        accStates(MAX_PLY + 1),
        index(0) {}

    [[nodiscard]] const AccumulatorState& latest() const noexcept;

    void reset(const Position& pos, const Networks& networks, AccumulatorCaches& caches) noexcept;
    void push(const DirtyPiece& dp) noexcept;
    void pop() noexcept;

    template<IndexType Dimensions, Accumulator<Dimensions> AccumulatorState::*accPtr>
    void evaluate(const Position&                               pos,
                  const FeatureTransformer<Dimensions, accPtr>& featureTransformer,
                  Cache<Dimensions>&                            cache) noexcept;

   private:
    [[nodiscard]] AccumulatorState& mut_latest() noexcept;

    template<Color                   Perspective,
             IndexType               Dimensions,
             Accumulator<Dimensions> AccumulatorState::*accPtr>
    void evaluate_side(const Position&                               pos,
                       const FeatureTransformer<Dimensions, accPtr>& featureTransformer,
                       Cache<Dimensions>&                            cache) noexcept;

    template<Color                   Perspective,
             IndexType               Dimensions,
             Accumulator<Dimensions> AccumulatorState::*accPtr>
    [[nodiscard]] std::size_t find_last_usable_accumulator() const noexcept;

    template<Color                   Perspective,
             IndexType               Dimensions,
             Accumulator<Dimensions> AccumulatorState::*accPtr>
    void
    forward_update_incremental(const Position&                               pos,
                               const FeatureTransformer<Dimensions, accPtr>& featureTransformer,
                               const std::size_t                             begin) noexcept;

    template<Color                   Perspective,
             IndexType               Dimensions,
             Accumulator<Dimensions> AccumulatorState::*accPtr>
    void
    backward_update_incremental(const Position&                               pos,
                                const FeatureTransformer<Dimensions, accPtr>& featureTransformer,
                                const std::size_t                             end) noexcept;

    std::vector<AccumulatorState> accStates;
    std::size_t                   index;
};

}  // namespace NNUE
}  // namespace DON

#endif  // #ifndef NNUE_ACCUMULATOR_H_INCLUDED
