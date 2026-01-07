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

#ifndef NNUE_NETWORK_H_INCLUDED
#define NNUE_NETWORK_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "../misc.h"
#include "accumulator.h"
#include "architecture.h"
#include "common.h"
#include "feature_transformer.h"
#include "nmisc.h"

namespace DON {

class Position;

namespace NNUE {

enum class EmbeddedType {
    BIG,
    SMALL
};

template<typename Arch, typename Transformer>
class Network final {
   private:
    static constexpr IndexType TFDimensions = Arch::TransformedFeatureDimensions;
    // Hash value of evaluation function structure
    static constexpr std::uint32_t Hash = Arch::hash() ^ Transformer::hash();

   public:
    Network(EvalFile evFile, EmbeddedType embType) noexcept :
        evalFile(evFile),
        embeddedType(embType) {}

    Network(const Network& net)            = default;
    Network(Network&&) noexcept            = default;
    Network& operator=(const Network& net) = default;
    Network& operator=(Network&&) noexcept = default;

    void load(std::string_view rootDirectory, std::string netFile) noexcept;
    bool save(const std::optional<std::string>& netFile) const noexcept;

    void verify(std::string netFile) const noexcept;

    std::size_t content_hash() const noexcept;

    NetworkOutput evaluate(const Position&                         pos,
                           AccumulatorStack&                       accStack,
                           AccumulatorCaches::Cache<TFDimensions>& cache) const noexcept;

    NetworkTrace trace(const Position&                         pos,
                       AccumulatorStack&                       accStack,
                       AccumulatorCaches::Cache<TFDimensions>& cache) const noexcept;

   private:
    void load_user_net(const std::string& dir, const std::string& netFile) noexcept;
    void load_internal() noexcept;

    void initialize() noexcept;

    bool save(std::ostream&      os,
              const std::string& name,
              const std::string& netDescription) const noexcept;

    std::optional<std::string> load(std::istream& is) noexcept;

    bool read_parameters(std::istream& is, std::string& netDescription) noexcept;
    bool write_parameters(std::ostream& os, const std::string& netDescription) const noexcept;

    // Input feature converter
    Transformer featureTransformer;

    // Evaluation function
    StdArray<Arch, LayerStacks> network;

    EvalFile     evalFile;
    EmbeddedType embeddedType;

    bool initialized = false;

    template<IndexType Size>
    friend struct AccumulatorCaches::Cache;
};

// Definitions of the network types
using BigNetworkArchitecture = NetworkArchitecture<BigTransformedFeatureDimensions, BigL2, BigL3>;
using BigFeatureTransformer  = FeatureTransformer<BigTransformedFeatureDimensions>;

using SmallNetworkArchitecture =
  NetworkArchitecture<SmallTransformedFeatureDimensions, SmallL2, SmallL3>;
using SmallFeatureTransformer = FeatureTransformer<SmallTransformedFeatureDimensions>;

using BigNetwork   = Network<BigNetworkArchitecture, BigFeatureTransformer>;
using SmallNetwork = Network<SmallNetworkArchitecture, SmallFeatureTransformer>;

struct Networks final {
   public:
    Networks(const EvalFile& bigFile, const EvalFile& smallFile) noexcept :
        big(bigFile, EmbeddedType::BIG),
        small(smallFile, EmbeddedType::SMALL) {}

    BigNetwork   big;
    SmallNetwork small;
};

}  // namespace NNUE
}  // namespace DON

template<typename Arch, typename FeatureTransformer>
struct std::hash<DON::NNUE::Network<Arch, FeatureTransformer>> {
    std::size_t
    operator()(const DON::NNUE::Network<Arch, FeatureTransformer>& network) const noexcept {
        return network.content_hash();
    }
};

template<>
struct std::hash<DON::NNUE::Networks> {
    std::size_t operator()(const DON::NNUE::Networks& networks) const noexcept {
        std::size_t h = 0;
        DON::combine_hash(h, networks.big);
        DON::combine_hash(h, networks.small);
        return h;
    }
};

#endif  // #ifndef NNUE_NETWORK_H_INCLUDED
