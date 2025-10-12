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

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../types.h"
#include "nnue_architecture.h"
#include "nnue_common.h"

namespace DON {

class Position;

namespace NNUE {

struct Networks;

template<IndexType TransformedFeatureDimensions>
class FeatureTransformer;

// Accumulator holds the result of affine transformation of input features
template<IndexType Size>
struct alignas(CACHE_LINE_SIZE) Accumulator final {
   public:
    constexpr Accumulator() noexcept = default;

    BiasType       accumulation[COLOR_NB][Size]{};
    PSQTWeightType psqtAccumulation[COLOR_NB][PSQTBuckets]{};
    bool           computed[COLOR_NB]{};
};

using BigAccumulator   = Accumulator<BigTransformedFeatureDimensions>;
using SmallAccumulator = Accumulator<SmallTransformedFeatureDimensions>;

// AccumulatorCaches provides per-thread accumulator caches,
// where each cache contains multiple entries for each of the possible king squares.
// When the accumulator needs to be refreshed, the cached entry is used to more
// efficiently update the accumulator, instead of rebuilding it from scratch.
// This idea, was first described by Luecx (author of Koivisto) and
// is commonly referred to as "Finny Tables".
struct AccumulatorCaches final {
   public:
    constexpr AccumulatorCaches() noexcept = default;

    template<IndexType Size>
    struct alignas(CACHE_LINE_SIZE) Cache final {
       public:
        constexpr Cache() noexcept = default;

        struct alignas(CACHE_LINE_SIZE) Entry final {
           public:
            constexpr Entry() noexcept = default;

            // To initialize a refresh entry, set all its bitboards empty,
            // so put the biases in the accumulation, without any weights on top
            void init(const BiasType* biases) noexcept {
                // Initialize accumulation with given biases
                std::memcpy(accumulation, biases, sizeof(accumulation));
                auto offset = offsetof(Entry, psqtAccumulation);
                std::memset(reinterpret_cast<std::uint8_t*>(this) + offset, 0,
                            sizeof(*this) - offset);
            }

            BiasType       accumulation[Size]{};
            PSQTWeightType psqtAccumulation[PSQTBuckets]{};
            Bitboard       colorBB[COLOR_NB]{};
            Bitboard       typeBB[PIECE_TYPE_NB]{};
        };

        template<typename Network>
        void init(const Network& network) noexcept {

            for (auto& subEntry : entries)
                for (auto& entry : subEntry)
                    entry.init(network.featureTransformer->biases);
        }

        auto& operator[](Square s) noexcept { return entries[s]; }

        Entry entries[SQUARE_NB][COLOR_NB]{};
    };

    using BigCache   = Cache<BigTransformedFeatureDimensions>;
    using SmallCache = Cache<SmallTransformedFeatureDimensions>;

    explicit AccumulatorCaches(const Networks& networks) noexcept { init(networks); }

    void init(const Networks& networks) noexcept;

    BigCache   big;
    SmallCache small;
};

struct AccumulatorState final {
   public:
    AccumulatorState() noexcept = default;

    template<IndexType Size>
    const auto& acc() const noexcept {
        static_assert(Size == BigTransformedFeatureDimensions
                        || Size == SmallTransformedFeatureDimensions,
                      "Invalid size for accumulator");

        if constexpr (Size == BigTransformedFeatureDimensions)
            return big;
        else if constexpr (Size == SmallTransformedFeatureDimensions)
            return small;
    }
    template<IndexType Size>
    auto& acc() noexcept {
        static_assert(Size == BigTransformedFeatureDimensions
                        || Size == SmallTransformedFeatureDimensions,
                      "Invalid size for accumulator");

        if constexpr (Size == BigTransformedFeatureDimensions)
            return big;
        else if constexpr (Size == SmallTransformedFeatureDimensions)
            return small;
    }

    void reset(const DirtyPiece& dp) noexcept;

    DirtyPiece       dirtyPiece;
    BigAccumulator   big;
    SmallAccumulator small;
};

struct AccumulatorStack final {
   public:
    constexpr AccumulatorStack() noexcept = default;

    [[nodiscard]] const AccumulatorState& clatest_state() const noexcept;
    [[nodiscard]] AccumulatorState&       latest_state() noexcept;

    void reset() noexcept;
    void push(const DirtyPiece& dp) noexcept;
    void pop() noexcept;

    template<IndexType Dimensions>
    void evaluate(const Position&                       pos,
                  const FeatureTransformer<Dimensions>& featureTransformer,
                  AccumulatorCaches::Cache<Dimensions>& cache) noexcept;

   private:
    template<Color Perspective, IndexType Dimensions>
    void evaluate_side(const Position&                       pos,
                       const FeatureTransformer<Dimensions>& featureTransformer,
                       AccumulatorCaches::Cache<Dimensions>& cache) noexcept;

    template<Color Perspective, IndexType Dimensions>
    [[nodiscard]] std::size_t find_last_usable_accumulator() const noexcept;

    template<Color Perspective, IndexType Dimensions>
    void forward_update_incremental(const Position&                       pos,
                                    const FeatureTransformer<Dimensions>& featureTransformer,
                                    std::size_t                           begin) noexcept;

    template<Color Perspective, IndexType Dimensions>
    void backward_update_incremental(const Position&                       pos,
                                     const FeatureTransformer<Dimensions>& featureTransformer,
                                     std::size_t                           end) noexcept;

    AccumulatorState accStates[MAX_PLY + 1]{};
    std::size_t      size{1};
};

}  // namespace NNUE
}  // namespace DON

#endif  // #ifndef NNUE_ACCUMULATOR_H_INCLUDED
