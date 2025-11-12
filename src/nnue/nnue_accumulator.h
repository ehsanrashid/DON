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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "nnue_architecture.h"
#include "nnue_common.h"

namespace DON {

namespace NNUE {

template<IndexType TransformedFeatureDimensions>
class FeatureTransformer;

// Accumulator holds the result of affine transformation of input features
template<IndexType Size>
struct alignas(CACHE_LINE_SIZE) Accumulator final {
   public:
    constexpr Accumulator() noexcept = default;

    StdArray<BiasType, COLOR_NB, Size>              accumulation{};
    StdArray<PSQTWeightType, COLOR_NB, PSQTBuckets> psqtAccumulation{};
    StdArray<bool, COLOR_NB>                        computed{};
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
    template<IndexType Size>
    struct alignas(CACHE_LINE_SIZE) Cache final {
       public:
        constexpr Cache() noexcept = default;

        struct alignas(CACHE_LINE_SIZE) Entry final {
           public:
            constexpr Entry() noexcept = default;

            // To initialize a refresh entry, set all its bitboards empty,
            // so put the biases in the accumulation, without any weights on top
            void init(const StdArray<BiasType, Size>& biases) noexcept {
                // Initialize accumulation with given biases
                accumulation = biases;
                auto offset  = offsetof(Entry, psqtAccumulation);
                assert(offset <= sizeof(*this) && "offset exceeds object size");
                std::memset(reinterpret_cast<std::uint8_t*>(this) + offset, 0,
                            sizeof(*this) - offset);
            }

            StdArray<BiasType, Size>              accumulation{};
            StdArray<PSQTWeightType, PSQTBuckets> psqtAccumulation{};
            Bitboard                              pieces{};
            Position::PieceArray                  pieceArr{};
        };

        template<typename Network>
        void init(const Network& network) noexcept {

            for (auto& sqEntries : entries)
                for (auto& entry : sqEntries)
                    entry.init(network.featureTransformer.biases);
        }

        const StdArray<Entry, COLOR_NB>& operator[](Square s) const noexcept { return entries[s]; }
        StdArray<Entry, COLOR_NB>&       operator[](Square s) noexcept { return entries[s]; }

        StdArray<Entry, SQUARE_NB, COLOR_NB> entries{};
    };

    using BigCache   = Cache<BigTransformedFeatureDimensions>;
    using SmallCache = Cache<SmallTransformedFeatureDimensions>;

    template<typename Networks>
    explicit AccumulatorCaches(const Networks& networks) noexcept {
        init(networks);
    }

    template<typename Networks>
    void init(const Networks& networks) noexcept {
        big.init(networks.big);
        small.init(networks.small);
    }

    BigCache   big{};
    SmallCache small{};
};

template<typename FeatureSet>
struct AccumulatorState final {
   public:
    constexpr AccumulatorState() noexcept = default;

    template<IndexType Size>
    const auto& acc() const noexcept {
        static_assert(Size == BigTransformedFeatureDimensions
                        || Size == SmallTransformedFeatureDimensions,
                      "Invalid size for accumulator");

        if constexpr (Size == BigTransformedFeatureDimensions)
            return big;
        if constexpr (Size == SmallTransformedFeatureDimensions)
            return small;
    }
    template<IndexType Size>
    auto& acc() noexcept {
        static_assert(Size == BigTransformedFeatureDimensions
                        || Size == SmallTransformedFeatureDimensions,
                      "Invalid size for accumulator");

        if constexpr (Size == BigTransformedFeatureDimensions)
            return big;
        if constexpr (Size == SmallTransformedFeatureDimensions)
            return small;
    }

    void reset(const typename FeatureSet::DirtyType& dt) noexcept {
        dirtyType = dt;
        big.computed.fill(false);
        small.computed.fill(false);
    }

    typename FeatureSet::DirtyType dirtyType{};
    BigAccumulator                 big{};
    SmallAccumulator               small{};
};

struct AccumulatorStack final {
   public:
    static constexpr std::size_t MaxSize = MAX_PLY + 1;

    constexpr AccumulatorStack() noexcept = default;

    template<typename T>
    [[nodiscard]] const StdArray<AccumulatorState<T>, MaxSize>& accumulators() const noexcept;

    template<typename T>
    [[nodiscard]] const AccumulatorState<T>& state() const noexcept;

    void reset() noexcept;
    void push(const DirtyBoard& db) noexcept;
    void pop() noexcept;

    template<IndexType Dimensions>
    void evaluate(const Position&                       pos,
                  const FeatureTransformer<Dimensions>& featureTransformer,
                  AccumulatorCaches::Cache<Dimensions>& cache) noexcept;

   private:
    template<typename T>
    [[nodiscard]] StdArray<AccumulatorState<T>, MaxSize>& mut_accumulators() noexcept;

    template<typename T>
    [[nodiscard]] AccumulatorState<T>& mut_state() noexcept;


    template<typename FeatureSet, IndexType Dimensions>
    void evaluate(Color                                 perspective,
                  const Position&                       pos,
                  const FeatureTransformer<Dimensions>& featureTransformer,
                  AccumulatorCaches::Cache<Dimensions>& cache) noexcept;

    template<typename FeatureSet, IndexType Dimensions>
    [[nodiscard]] std::size_t last_usable_accumulator_index(Color perspective) const noexcept;

    template<typename FeatureSet, IndexType Dimensions>
    void forward_update_incremental(Color                                 perspective,
                                    const Position&                       pos,
                                    const FeatureTransformer<Dimensions>& featureTransformer,
                                    std::size_t                           begin) noexcept;

    template<typename FeatureSet, IndexType Dimensions>
    void backward_update_incremental(Color                                 perspective,
                                     const Position&                       pos,
                                     const FeatureTransformer<Dimensions>& featureTransformer,
                                     std::size_t                           end) noexcept;

    StdArray<AccumulatorState<PSQFeatureSet>, MaxSize>    psqAccumulators;
    StdArray<AccumulatorState<ThreatFeatureSet>, MaxSize> threatAccumulators;
    std::size_t                                           size{1};
};

}  // namespace NNUE
}  // namespace DON

#endif  // #ifndef NNUE_ACCUMULATOR_H_INCLUDED
