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

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

#include "../misc.h"
#include "architecture.h"
#include "feature_transformer.h"
#include "ntypes.h"

namespace DON {

class Position;

namespace NNUE {

struct AccumulatorCache;
struct AccumulatorStack;

class Network final {
   private:
    // Hash value of evaluation function structure
    static constexpr u32 Hash = NetworkArchitecture::hash() ^ FeatureTransformer::hash();

   public:
    Network() noexcept                     = default;
    Network(const Network&)                = default;
    Network(Network&&) noexcept            = default;
    Network& operator=(const Network&)     = default;
    Network& operator=(Network&&) noexcept = default;

    void load(const std::filesystem::path& rootDirectory,
              std::filesystem::path        evalFilePath,
              EvalFile&                    evalFile) noexcept;
    bool save(const std::optional<std::filesystem::path>& evalFilePath,
              const EvalFile&                             evalFile) const noexcept;

    void verify(std::filesystem::path evalFilePath, const EvalFile& evalFile) const noexcept;

    usize content_hash() const noexcept;

    NetworkOutput evaluate(const Position&   pos,
                           AccumulatorCache& accCache,
                           AccumulatorStack& accStack) const noexcept;

    NetworkTrace trace(const Position&   pos,
                       AccumulatorCache& accCache,
                       AccumulatorStack& accStack) const noexcept;

   private:
    bool load_embedded(EvalFile& evalFile) noexcept;
    bool load_external(const std::filesystem::path& dir,
                       const std::filesystem::path& evalFilePath,
                       EvalFile&                    evalFile) noexcept;

    std::optional<std::string> load(std::istream& is) noexcept;
    bool save(std::ostream& os, std::string_view netDescription) const noexcept;

    bool read_parameters(std::istream& is, std::string& netDescription) noexcept;
    bool write_parameters(std::ostream& os, std::string_view netDescription) const noexcept;

    // Input feature converter
    FeatureTransformer featureTransformer;

    // Evaluation function
    Array<NetworkArchitecture, LayerStacks> networkArchitectures;

    bool initialized = false;

    friend struct AccumulatorCache;
};

}  // namespace NNUE
}  // namespace DON

template<>
struct std::hash<DON::NNUE::Network> {
    DON::usize operator()(const DON::NNUE::Network& network) const noexcept {
        return network.content_hash();
    }
};

#endif  // #ifndef NNUE_NETWORK_H_INCLUDED
