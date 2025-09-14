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

// Definition of layer ClippedReLU of NNUE evaluation function

#ifndef NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED
#define NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED

#include <algorithm>
#include <cstdint>
#include <iosfwd>

#include "../nnue_common.h"

namespace DON::NNUE::Layers {

// Clipped ReLU
template<IndexType InDims>
class SqrClippedReLU {
   public:
    // Input/output type
    using InputType  = std::int32_t;
    using OutputType = std::uint8_t;

    // Number of input/output dimensions
    static constexpr IndexType InputDimensions  = InDims;
    static constexpr IndexType OutputDimensions = InputDimensions;
    static constexpr IndexType PaddedOutputDimensions =
      ceil_to_multiple<IndexType>(OutputDimensions, 32);

    using OutputBuffer = OutputType[PaddedOutputDimensions];

    // Hash value embedded in the evaluation file
    static constexpr std::uint32_t get_hash_value(std::uint32_t preHashValue) noexcept {
        std::uint32_t hashValue = 0x538D24C7U;
        hashValue += preHashValue;
        return hashValue;
    }

    // Read network parameters
    bool read_parameters(std::istream&) noexcept { return true; }

    // Write network parameters
    bool write_parameters(std::ostream&) const noexcept { return true; }

    // Forward propagation
    void propagate(const InputType* input, OutputType* output) const noexcept {

#if defined(USE_SSE2)
        constexpr IndexType ChunkCount = InputDimensions / 16;

        static_assert(WEIGHT_SCALE_BITS == 6);

        const auto* in  = reinterpret_cast<const __m128i*>(input);
        auto*       out = reinterpret_cast<__m128i*>(output);
        for (IndexType i = 0; i < ChunkCount; ++i)
        {
            __m128i words0 =
              _mm_packs_epi32(_mm_load_si128(&in[i * 4 + 0]), _mm_load_si128(&in[i * 4 + 1]));
            __m128i words1 =
              _mm_packs_epi32(_mm_load_si128(&in[i * 4 + 2]), _mm_load_si128(&in[i * 4 + 3]));

            // Shift by WEIGHT_SCALE_BITS * 2 = 12 and divide by 128
            // which is an additional shift-right of 7, meaning 19 in total.
            // MulHi strips the lower 16 bits so need to shift out 3 more to match.
            words0 = _mm_srli_epi16(_mm_mulhi_epi16(words0, words0), 3);
            words1 = _mm_srli_epi16(_mm_mulhi_epi16(words1, words1), 3);

            _mm_store_si128(&out[i], _mm_packs_epi16(words0, words1));
        }
        constexpr IndexType Start = 16 * ChunkCount;
#else
        constexpr IndexType Start = 0;
#endif

        for (IndexType i = Start; i < InputDimensions; ++i)
        {
            output[i] = static_cast<OutputType>(
              // Really should be /127 but need to make it fast so right-shift
              // by an extra 7 bits instead. Needs to be accounted for in the trainer.
              std::min(((long long) (input[i]) * input[i]) >> (7 + 2 * WEIGHT_SCALE_BITS), 127ll));
        }
    }
};

}  // namespace DON::NNUE::Layers

#endif  // #ifndef NNUE_LAYERS_SQR_CLIPPED_RELU_H_INCLUDED
