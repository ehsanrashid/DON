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

#ifndef NETWORK_H_INCLUDED
#define NETWORK_H_INCLUDED

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <utility>

#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "nnue_architecture.h"
#include "nnue_feature_transformer.h"
#include "nnue_misc.h"

namespace DON {

namespace Eval::NNUE {

enum class EmbeddedNNUEType {
    BIG,
    SMALL,
};

template<typename Arch, typename Transformer>
class Network final {
   public:
    Network(EvalFile file, EmbeddedNNUEType type) noexcept :
        evalFile(file),
        embeddedType(type) {}

    void load(const std::string& rootDirectory, std::string evalfilePath) noexcept;
    bool save(const std::optional<std::string>& filename) const noexcept;

    Value evaluate(const Position& pos,
                   bool            adjusted   = false,
                   int*            complexity = nullptr,
                   bool            psqtOnly   = false) const noexcept;

    void hint_common_access(const Position& pos, bool psqtOnly) const noexcept;

    void          verify(std::string evalfilePath) const noexcept;
    NnueEvalTrace trace_evaluate(const Position& pos) const noexcept;

   private:
    void load_user_net(const std::string& dir, const std::string& evalfilePath) noexcept;
    void load_internal() noexcept;

    void initialize() noexcept;

    bool                       save(std::ostream&      stream,
                                    const std::string& name,
                                    const std::string& netDescription) const noexcept;
    std::optional<std::string> load(std::istream& stream) noexcept;

    bool read_header(std::istream&, std::uint32_t*, std::string*) const noexcept;
    bool write_header(std::ostream&, std::uint32_t, const std::string&) const noexcept;

    bool read_parameters(std::istream&, std::string&) const noexcept;
    bool write_parameters(std::ostream&, const std::string&) const noexcept;

    // Input feature converter
    LargePagePtr<Transformer> featureTransformer;

    // Evaluation function
    AlignedPtr<Arch> network[LayerStacks];

    EvalFile         evalFile;
    EmbeddedNNUEType embeddedType;

    // Hash value of evaluation function structure
    static constexpr std::uint32_t Hash = Transformer::get_hash_value() ^ Arch::get_hash_value();
};

// Definitions of the network types
using BigNetworkArchitecture = NetworkArchitecture<TransformedFeatureDimensionsBig, L2Big, L3Big>;
using BigFeatureTransformer =
  FeatureTransformer<TransformedFeatureDimensionsBig, &StateInfo::accumulatorBig>;

using SmallNetworkArchitecture =
  NetworkArchitecture<TransformedFeatureDimensionsSmall, L2Small, L3Small>;
using SmallFeatureTransformer =
  FeatureTransformer<TransformedFeatureDimensionsSmall, &StateInfo::accumulatorSmall>;

using BigNetwork   = Network<BigNetworkArchitecture, BigFeatureTransformer>;
using SmallNetwork = Network<SmallNetworkArchitecture, SmallFeatureTransformer>;

struct Networks final {
    Networks(BigNetwork&& bigNet, SmallNetwork&& smallNet) noexcept :
        big(std::move(bigNet)),
        small(std::move(smallNet)) {}

    BigNetwork   big;
    SmallNetwork small;
};

}  // namespace Eval::NNUE
}  // namespace DON

#endif  // #ifndef NETWORK_H_INCLUDED
