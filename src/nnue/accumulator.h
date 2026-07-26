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

#include <cassert>
#include <cstddef>
#include <cstring>

#include "../misc.h"
#include "../types.h"
#include "architecture.h"
#include "common.h"

namespace DON {

class Position;

namespace NNUE {

class FeatureTransformer;

// Accumulator holds the result of affine transformation of input features
struct alignas(CACHE_LINE_SIZE) Accumulator {
   public:
    Array<BiasType, COLOR_NB, L1>                accumulation;
    Array<PSQTWeightType, COLOR_NB, PSQTBuckets> psqtAccumulation;
    Array<bool, COLOR_NB>                        computed;
};

// AccumulatorCaches provides per-thread accumulator caches,
// where each cache contains multiple entries for each of the possible king squares.
// When the accumulator needs to be refreshed, the cached entry is used to more
// efficiently update the accumulator, instead of rebuilding it from scratch.
// This idea, was first described by Luecx (author of Koivisto) and
// is commonly referred to as "Finny Tables".
struct AccumulatorCaches final {
   public:
    struct alignas(CACHE_LINE_SIZE) Entry final {
       public:
        // To initialize a refresh entry, set all its bitboards empty,
        // so put the biases in the accumulation, without any weights on top
        void init(const Array<BiasType, L1>& biases) noexcept {
            // Initialize accumulation with given biases
            accumulation = biases;
            auto offset  = offsetof(Entry, psqtAccumulation);
            assert(offset <= sizeof(*this) && "offset exceeds object size");
            std::memset(reinterpret_cast<unsigned char*>(this) + offset, 0, sizeof(*this) - offset);
        }

        Array<BiasType, L1>                accumulation;
        Array<PSQTWeightType, PSQTBuckets> psqtAccumulation;
        Array<Piece, SQUARE_NB>            pieceMap;
        Bitboard                           piecesBB;
    };

    template<typename Network>
    explicit AccumulatorCaches(const Network& network) noexcept {
        init(network);
    }

    template<typename Network>
    void init(const Network& network) noexcept {
        for (auto& sqEntries : entries)
            for (auto& entry : sqEntries)
                entry.init(network.featureTransformer.biases);
    }

    const Array<Entry, COLOR_NB>& operator[](Square s) const noexcept { return entries[s]; }
    Array<Entry, COLOR_NB>&       operator[](Square s) noexcept { return entries[s]; }

   private:
    Array<Entry, SQUARE_NB, COLOR_NB> entries;
};

template<typename FeatureSet>
struct AccumulatorState final: public Accumulator {
   public:
    typename FeatureSet::DirtyType dirty;

    void reset(typename FeatureSet::DirtyType&& dt) noexcept {
        dirty = std::move(dt);
        computed.fill(false);
    }
};

struct AccumulatorStack final {
   public:
    static constexpr usize SIZE = PLY_MAX + 1;

    template<typename T>
    [[nodiscard]] const Array<AccumulatorState<T>, SIZE>& accumulators() const noexcept;

    template<typename T>
    [[nodiscard]] const AccumulatorState<T>& state() const noexcept;

    void reset() noexcept;
    void push(DirtyBoard&& db) noexcept;
    void pop() noexcept;

    void evaluate(const Position&           pos,
                  const FeatureTransformer& featureTransformer,
                  // Silence spurious warning on GCC 10
                  [[maybe_unused]] AccumulatorCaches& cache) noexcept;

   private:
    template<typename T>
    [[nodiscard]] Array<AccumulatorState<T>, SIZE>& mut_accumulators() noexcept;

    template<typename T>
    [[nodiscard]] AccumulatorState<T>& mut_state() noexcept;

    template<typename FeatureSet>
    void evaluate(Color                     perspective,
                  const Position&           pos,
                  const FeatureTransformer& featureTransformer,
                  // Silence spurious warning on GCC 10
                  [[maybe_unused]] AccumulatorCaches& cache) noexcept;

    template<typename FeatureSet>
    [[nodiscard]] usize last_usable_accumulator_index(Color perspective) const noexcept;

    template<typename FeatureSet>
    void update_forward_incr(Color                     perspective,
                             const Position&           pos,
                             const FeatureTransformer& featureTransformer,
                             usize                     beg) noexcept;

    template<typename FeatureSet>
    void update_backward_incr(Color                     perspective,
                              const Position&           pos,
                              const FeatureTransformer& featureTransformer,
                              usize                     end) noexcept;

    Array<AccumulatorState<PSQFeatureSet>, SIZE>    psqAccumulators;
    Array<AccumulatorState<ThreatFeatureSet>, SIZE> threatAccumulators;
    usize                                           size = 1;
};

}  // namespace NNUE
}  // namespace DON

#endif  // #ifndef NNUE_ACCUMULATOR_H_INCLUDED
