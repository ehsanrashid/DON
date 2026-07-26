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

#include <iosfwd>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "../misc.h"
#include "architecture.h"
#include "feature_transformer.h"
#include "nmisc.h"

namespace DON {

class Position;

namespace NNUE {

struct AccumulatorCaches;
struct AccumulatorStack;

class Network final {
   private:
    // Hash value of evaluation function structure
    static constexpr u32 Hash = NetworkArchitecture::hash() ^ FeatureTransformer::hash();

   public:
    Network(const EvalFile& evFile) noexcept :
        evalFile(evFile) {}

    Network(const Network&)                = default;
    Network(Network&&) noexcept            = default;
    Network& operator=(const Network&)     = default;
    Network& operator=(Network&&) noexcept = default;

    void load(std::string_view rootDirectory, std::string_view netFile) noexcept;
    bool save(std::string_view netFile) const noexcept;

    void verify(std::string_view netFile) const noexcept;

    usize content_hash() const noexcept;

    NetworkOutput evaluate(const Position&    pos,
                           AccumulatorStack&  accStack,
                           AccumulatorCaches& cache) const noexcept;

    NetworkTrace
    trace(const Position& pos, AccumulatorStack& accStack, AccumulatorCaches& cache) const noexcept;

   private:
    std::optional<std::string> load(std::istream& is) noexcept;

    bool load_embedded() noexcept;
    bool load_file(std::string_view dir, std::string_view netFile) noexcept;

    bool
    save(std::ostream& os, std::string_view name, std::string_view netDescription) const noexcept;

    bool read_parameters(std::istream& is, std::string& netDescription) noexcept;
    bool write_parameters(std::ostream& os, const std::string& netDescription) const noexcept;

    // Input feature converter
    FeatureTransformer featureTransformer;

    // Evaluation function
    Array<NetworkArchitecture, LayerStacks> network;

    EvalFile evalFile;

    bool initialized = false;

    friend struct AccumulatorCaches;
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
