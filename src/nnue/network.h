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
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "../memory.h"
#include "../position.h"
#include "../types.h"
#include "nnue_accumulator.h"
#include "nnue_architecture.h"
#include "nnue_feature_transformer.h"
#include "nnue_misc.h"

namespace DON::NNUE {

enum class EmbeddedNNUEType {
    BIG,
    SMALL,
};

struct NetworkOutput final {
    std::int32_t psqt;
    std::int32_t positional;
};

template<typename Arch, typename Transformer>
class Network final {
    static constexpr IndexType TransformedFeatureDimensions = Arch::TransformedFeatureDimensions;

   public:
    Network(EvalFile file, EmbeddedNNUEType type) noexcept :
        evalFile(file),
        embeddedType(type) {}

    Network(const Network<Arch, Transformer>& net) noexcept;
    Network(Network<Arch, Transformer>&&) noexcept = default;
    Network<Arch, Transformer>& operator=(const Network<Arch, Transformer>& net) noexcept;
    Network<Arch, Transformer>& operator=(Network<Arch, Transformer>&&) noexcept = default;

    void load(const std::string& rootDirectory, std::string evalfilePath) noexcept;
    bool save(const std::optional<std::string>& filename) const noexcept;

    NetworkOutput
    evaluate(const Position&                                         pos,
             AccumulatorCaches::Cache<TransformedFeatureDimensions>* cache) const noexcept;

    void hint_common_access(
      const Position&                                         pos,
      AccumulatorCaches::Cache<TransformedFeatureDimensions>* cache) const noexcept;

    void verify(std::string evalfilePath,
                const std::function<void(std::string_view)>&) const noexcept;
    NnueEvalTrace
    trace_eval(const Position&                                         pos,
               AccumulatorCaches::Cache<TransformedFeatureDimensions>* cache) const noexcept;

   private:
    void load_user_net(const std::string& dir, const std::string& evalfilePath) noexcept;
    void load_internal() noexcept;

    void initialize() noexcept;

    bool                       save(std::ostream&      ostream,
                                    const std::string& name,
                                    const std::string& netDescription) const noexcept;
    std::optional<std::string> load(std::istream& istream) noexcept;

    bool read_header(std::istream&, std::uint32_t*, std::string*) const noexcept;
    bool write_header(std::ostream&, std::uint32_t, const std::string&) const noexcept;

    bool read_parameters(std::istream&, std::string&) const noexcept;
    bool write_parameters(std::ostream&, const std::string&) const noexcept;

    // Hash value of evaluation function structure
    static constexpr std::uint32_t HASH_VALUE =
      Arch::get_hash_value() ^ Transformer::get_hash_value();

    // Input feature converter
    AlignedLPPtr<Transformer> featureTransformer;

    // Evaluation function
    AlignedStdPtr<Arch[]> network;

    EvalFile         evalFile;
    EmbeddedNNUEType embeddedType;

    template<IndexType Size>
    friend struct AccumulatorCaches::Cache;
};

// Definitions of the network types
using BigNetworkArchitecture = NetworkArchitecture<BigTransformedFeatureDimensions, BigL2, BigL3>;
using BigFeatureTransformer =
  FeatureTransformer<BigTransformedFeatureDimensions, &State::bigAccumulator>;

using SmallNetworkArchitecture =
  NetworkArchitecture<SmallTransformedFeatureDimensions, SmallL2, SmallL3>;
using SmallFeatureTransformer =
  FeatureTransformer<SmallTransformedFeatureDimensions, &State::smallAccumulator>;

using BigNetwork   = Network<BigNetworkArchitecture, BigFeatureTransformer>;
using SmallNetwork = Network<SmallNetworkArchitecture, SmallFeatureTransformer>;

struct Networks final {
    Networks(BigNetwork&& bigNet, SmallNetwork&& smallNet) noexcept :
        big(std::move(bigNet)),
        small(std::move(smallNet)) {}

    BigNetwork   big;
    SmallNetwork small;
};

}  // namespace DON::NNUE

#endif  // #ifndef NETWORK_H_INCLUDED