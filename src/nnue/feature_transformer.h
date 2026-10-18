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

#ifndef NNUE_FEATURE_TRANSFORMER_H_INCLUDED
#define NNUE_FEATURE_TRANSFORMER_H_INCLUDED

#include <array>
#include <cstring>
#include <functional>
#include <iosfwd>

#include "../position.h"
#include "../misc.h"
#include "../types.h"
#include "../nnue/accumulator.h"
#include "../nnue/types.h"
#include "../nnue/features/full_threats.h"
#include "../nnue/features/k32q2.h"
#include "../nnue/features/pp_3wide.h"

namespace DON::NNUE {

template<Index Dimensions>
struct NNZ;

// A class that converts the input features of the NNUE evaluation function

// Returns the inverse of a permutation
template<usize Size>
constexpr Array<usize, Size> invert_permutation(const std::array<usize, Size>& order) noexcept {
    Array<usize, Size> inverse{};
    for (usize i = 0; i < order.size(); ++i)
        inverse[order[i]] = i;
    return inverse;
}

// Divide a byte region of size TotalSize to chunks of size BlockSize,
// and permute the blocks by a given order
template<usize BlockSize, typename T, usize DataSize, usize OrderSize>
constexpr void permute(std::array<T, DataSize>&            data,
                       const std::array<usize, OrderSize>& order) noexcept {
    constexpr usize TotalSize = DataSize * sizeof(T);
    constexpr usize ChunkSize = BlockSize * OrderSize;
    static_assert(TotalSize % ChunkSize == 0, "ChunkSize must perfectly divide TotalSize");

    auto* byts = reinterpret_cast<u8*>(data.data());

    for (usize i = 0; i < TotalSize; i += ChunkSize)
    {
        auto* values = &byts[i];

        Array<u8, ChunkSize> buffer;

        for (usize j = 0; j < OrderSize; ++j)
        {
            auto* valueChunk  = &values[order[j] * BlockSize];
            auto* bufferChunk = &buffer[j * BlockSize];

            std::memcpy(bufferChunk, valueChunk, BlockSize);
        }

        std::memcpy(values, buffer.data(), ChunkSize);
    }
}

// Input feature converter
class FeatureTransformer final {
   private:
    // Number of output dimensions for one side
    static constexpr Index HalfDimensions = L1;

   public:
    // Output type
    using Output = TransformedFeature;

    // Number of input/output dimensions
    static constexpr usize ThreatInputDimensions = Features::FullThreats::Dimensions;
    static constexpr usize PairInputDimensions   = Features::PP3Wide::Dimensions;
    static constexpr usize PsqDimensions         = Features::K32Q2::Dimensions;

    static constexpr usize ThreatAndPpDimensions = ThreatInputDimensions + PairInputDimensions;
    static constexpr usize InputDimensions       = PsqDimensions + ThreatAndPpDimensions;

    static constexpr usize ThreatWeightSize      = ThreatInputDimensions * HalfDimensions;
    static constexpr usize PairWeightSize        = PairInputDimensions * HalfDimensions;
    static constexpr usize ThreatAndPpWeightSize = ThreatAndPpDimensions * HalfDimensions;
    static constexpr usize WeightArraySize       = PsqDimensions * HalfDimensions;

    static constexpr usize OutputDimensions = HalfDimensions;

    // clang-format off
    using ThreatAndPpWeightArray = Array<ThreatWeight, ThreatAndPpWeightSize>;
    using WeightArray            = Array<Weight      , WeightArraySize>;
    using BiasArray              = Array<Bias        , HalfDimensions>;
    // clang-format on

    // Size of forward propagation buffer
    static constexpr usize BufferSize = OutputDimensions * sizeof(Output);

    // Hash value embedded in the evaluation file
    static constexpr u32 hash() noexcept {
        return combine_hashes(
                 {Features::FullThreats::Hash, Features::PP3Wide::Hash, Features::K32Q2::Hash})
             ^ (2 * OutputDimensions);
    }

    // Store the order by which 128-bit blocks of a 1024-bit data must
    // be permuted so that calling packus on adjacent vectors of 16-bit
    // integers loaded from the data results in the pre-permutation order
    static constexpr auto PackusEpi16Order = []() -> Array<usize, 8> {
        return
#if defined(USE_AVX512)
          // _mm512_packus_epi16 after permutation:
          // |   0   |   2   |   4   |   6   | // Vector 0
          // |   1   |   3   |   5   |   7   | // Vector 1
          // | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | // Packed Result
          {0, 2, 4, 6, 1, 3, 5, 7};
#elif defined(USE_AVX2) || defined(USE_LASX)
          // _mm256_packus_epi16 after permutation:
          // |   0   |   2   |  |   4   |   6   | // Vector 0, 2
          // |   1   |   3   |  |   5   |   7   | // Vector 1, 3
          // | 0 | 1 | 2 | 3 |  | 4 | 5 | 6 | 7 | // Packed Result
          {0, 2, 1, 3, 4, 6, 5, 7};
#else
          {0, 1, 2, 3, 4, 5, 6, 7};
#endif
    }();

    static constexpr auto InversePackusEpi16Order = invert_permutation(PackusEpi16Order);

#if defined(USE_PAIRED_WIDENING_ADD)
    // The int8 weight rows are consumed one full vector register (sizeof(vec_t)
    // bytes) at a time: unpack{lo,hi}_epi8 interleave the bytes of two rows and
    // maddubs against a vector of ones sums each pair into an int16 lane. Both
    // unpacks work within 128-bit lanes, so the int16 results of a chunk come out
    // with its 8-byte blocks in the order {0, 2, 4, 6, ...} (lo) and {1, 3, 5, 7,
    // ...} (hi). Storing the 8-byte blocks of every chunk in this order at load
    // time makes the two output registers hold consecutive accumulator lanes, so
    // the accumulator layout is unchanged.
    static constexpr auto WideningPairOrder = []() constexpr noexcept {
        constexpr usize BlockCount = sizeof(SIMD::vec_t) / 8;

        Array<usize, BlockCount> order{};

        for (usize i = 0; i < BlockCount; ++i)
            order[i] = i % 2 == 0 ? i / 2 : BlockCount / 2 + i / 2;

        return order;
    }();

    static constexpr auto InverseWideningPairOrder = invert_permutation(WideningPairOrder);
#endif

    usize content_hash() const noexcept {
        usize h = 0;

        combine_hash(h, hash_raw_data(biases));
        combine_hash(h, hash_raw_data(weights));

        combine_hash(h, hash_raw_data(threatAndPpWeights));

        combine_hash(h, hash());

        return h;
    }

    template<bool Read>
    void permute_weights() noexcept {
        constexpr auto& Order = Read ? PackusEpi16Order : InversePackusEpi16Order;

        permute<16>(biases, Order);

        permute<8>(weights, Order);

        permute<8>(threatAndPpWeights, Order);

#if defined(USE_PAIRED_WIDENING_ADD)
        // With 128-bit vectors the order is the identity
        if constexpr (sizeof(SIMD::vec_t) > 16)
        {
            constexpr auto& PairOrder = Read ? WideningPairOrder : InverseWideningPairOrder;

            permute<8>(weights, PairOrder);

            permute<8>(threatAndPpWeights, PairOrder);
        }
#endif
    }

    auto* threatWeightData() noexcept { return threatAndPpWeights.data(); }
    auto* pawnPairWeightData() noexcept { return threatWeightData() + ThreatWeightSize; }

    // Read network parameters
    bool read_parameters(std::istream& is) noexcept;

    // Write network parameters
    bool write_parameters(std::ostream& os) const noexcept;

    // Convert input features
    void transform(const Position&            pos,
                   AccumulatorCache&          accCache,
                   AccumulatorStack&          accStack,
                   Array<Output, BufferSize>& output,
                   NNZ<OutputDimensions>&     nnz) const noexcept {

        accStack.evaluate(pos, *this, accCache);

        Array<Color, COLOR_NB> perspectives{pos.active_color(), ~pos.active_color()};

        const auto& accumulator = accStack.top();

        const auto& accumulation = accumulator.accumulation;

        transform_perspective(WHITE, accumulation[perspectives[WHITE]], output, nnz);
        transform_perspective(BLACK, accumulation[perspectives[BLACK]], output, nnz);
    }

    // clang-format off
    alignas(CACHE_LINE_SIZE) BiasArray              biases;
    alignas(CACHE_LINE_SIZE) ThreatAndPpWeightArray threatAndPpWeights;
    alignas(CACHE_LINE_SIZE) WeightArray            weights;
    // clang-format on

   private:
    static void transform_perspective(const Color                       perspective,
                                      const Array<i16, HalfDimensions>& accumulation,
                                      Array<Output, BufferSize>&        output,
                                      NNZ<OutputDimensions>&            nnz) noexcept;
};

}  // namespace DON::NNUE

template<>
struct std::hash<DON::NNUE::FeatureTransformer> {
    DON::usize operator()(const DON::NNUE::FeatureTransformer& ft) const noexcept {
        return ft.content_hash();
    }
};

#endif  // NNUE_FEATURE_TRANSFORMER_H_INCLUDED
