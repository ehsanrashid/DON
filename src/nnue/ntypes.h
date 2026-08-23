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

#ifndef NNUE_NTYPES_H_INCLUDED
#define NNUE_NTYPES_H_INCLUDED

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "../evaluate.h"
#include "../misc.h"

namespace DON::NNUE {

using BiasType         = i16;
using WeightType       = i16;
using PSQTWeightType   = i32;
using ThreatWeightType = i8;
using IndexType        = usize;

// Type of input feature after conversion
using TransformedFeatureType = u8;

// Number of input feature dimensions after conversion
inline constexpr IndexType L1 = 1024;
inline constexpr u32       L2 = 31;
inline constexpr u32       L3 = 32;

// Version of the evaluation file
inline constexpr u32 FILE_VERSION = 0x6A448AFAu;

inline constexpr IndexType PSQTBuckets = 8;
inline constexpr IndexType LayerStacks = 8;

// If vector instructions are enabled, update and refresh the accumulator
// tile by tile such that each tile fits in the CPU's vector registers.
static_assert(PSQTBuckets % 8 == 0,
              "Per feature PSQT values cannot be processed at granularity lower than 8 at a time.");

// Constant used in evaluation value calculation
inline constexpr i32 OUTPUT_SCALE      = 16;
inline constexpr u16 WEIGHT_SCALE_BITS = 6;
inline constexpr u16 FT_ONE            = 256;
inline constexpr u16 FT_MAX            = 255;
inline constexpr u16 HIDDEN_ONE        = 128;
inline constexpr u16 HIDDEN_MAX        = 127;

inline constexpr i64 Multiplier  = 600 * OUTPUT_SCALE;
inline constexpr i64 Denominator = (i64{1} << WEIGHT_SCALE_BITS) * HIDDEN_ONE * 2;

// EvalFile stores the currently selected evaluation network and its metadata.
// The network path may be explicitly selected through a UCI option or fall back to the default network,
// while the description is extracted from the loaded network file.
struct EvalFile final {
   public:
    EvalFile(std::optional<std::filesystem::path> curPath = std::nullopt,
             std::string_view                     netDesc = {}) noexcept :
        currentPath(std::move(curPath)),
        netDescription(netDesc) {}

    // Default net name, will use the EvalFileDefaultName macros defined in evaluate.h
    static constexpr std::string_view DefaultName = EvalFileDefaultName;
    // Selected net path, either via UCI option or default
    std::optional<std::filesystem::path> currentPath;
    // Net description extracted from the net file
    std::string netDescription;
};

struct NetworkOutput final {
   public:
    i32 psqt;
    i32 positional;
};

struct NetworkTrace final {
   public:
    Array<NetworkOutput, LayerStacks> netOut;
    usize                             correctBucket;
};

}  // namespace DON::NNUE

#endif  // NNUE_NTYPES_H_INCLUDED
