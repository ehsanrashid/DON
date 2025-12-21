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

#include "network.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <type_traits>

#define INCBIN_SILENCE_BITCODE_WARNING
#include "../incbin/incbin.h"

#include "../evaluate.h"
#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "../uci.h"
#include "common.h"

// Macro to embed the default efficiently updatable neural network (NNUE) file
// data in the engine binary (using incbin.h, by Dale Weiler).
// This macro invocation will declare the following three variables
//     const unsigned char        gEmbeddedData[];  // pointer to the embedded data
//     const unsigned char *const gEmbeddedEnd;     // marker to the embedded end
//     const unsigned int         gEmbeddedSize;    // size of the embedded file
// Note that this does not work in Microsoft Visual Studio.
#if !defined(_MSC_VER) && !defined(NNUE_EMBEDDING_OFF)
INCBIN(BigEmbedded, EvalFileDefaultNameBig);
INCBIN(SmallEmbedded, EvalFileDefaultNameSmall);
#else
const unsigned char        gBigEmbeddedData[1]   = {0x0};
const unsigned char* const gBigEmbeddedEnd       = &gBigEmbeddedData[1];
const unsigned int         gBigEmbeddedSize      = 1;
const unsigned char        gSmallEmbeddedData[1] = {0x0};
const unsigned char* const gSmallEmbeddedEnd     = &gSmallEmbeddedData[1];
const unsigned int         gSmallEmbeddedSize    = 1;
#endif

namespace DON::NNUE {

namespace {

struct Embedded final {
   public:
    Embedded(const unsigned char*       embeddedData,
             const unsigned char* const embeddedEnd,
             const unsigned int         embeddedSize) noexcept :
        data(embeddedData),
        end(embeddedEnd),
        size(embeddedSize) {}

    const unsigned char*       data;
    const unsigned char* const end;
    const unsigned int         size;
};

Embedded get_embedded(EmbeddedType embType) noexcept {
    assert(embType == EmbeddedType::BIG || embType == EmbeddedType::SMALL);

    return embType == EmbeddedType::BIG
           ? Embedded(gBigEmbeddedData, gBigEmbeddedEnd, gBigEmbeddedSize)
           : Embedded(gSmallEmbeddedData, gSmallEmbeddedEnd, gSmallEmbeddedSize);
}

// Read network header
bool _read_header(std::istream& is, std::uint32_t& hash, std::string& netDescription) noexcept {
    std::uint32_t fileVersion, descSize;
    fileVersion = read_little_endian<std::uint32_t>(is);
    hash        = read_little_endian<std::uint32_t>(is);
    descSize    = read_little_endian<std::uint32_t>(is);
    if (!is || fileVersion != FILE_VERSION)
        return false;
    netDescription.resize(descSize);
    is.read(netDescription.data(), descSize);

    return !is.fail();
}

// Write network header
bool _write_header(std::ostream&      os,
                   std::uint32_t      hash,
                   const std::string& netDescription) noexcept {
    write_little_endian<std::uint32_t>(os, FILE_VERSION);
    write_little_endian<std::uint32_t>(os, hash);
    write_little_endian<std::uint32_t>(os, netDescription.size());
    os.write(netDescription.data(), netDescription.size());

    return !os.fail();
}

// Read evaluation function parameters
template<typename T>
bool _read_parameters(std::istream& is, T& reference) noexcept {
    std::uint32_t hash;
    hash = read_little_endian<std::uint32_t>(is);
    if (!is || hash != T::hash())
        return false;

    return reference.read_parameters(is);
}

// Write evaluation function parameters
template<typename T>
bool _write_parameters(std::ostream& os, const T& reference) noexcept {
    write_little_endian<std::uint32_t>(os, T::hash());

    return reference.write_parameters(os);
}

}  // namespace

template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::load(std::string_view rootDirectory,
                                      std::string      netFile) noexcept {

    const Strings dirs{"<internal>", "", std::string(rootDirectory)
#if defined(DEFAULT_NNUE_DIRECTORY)
                                           ,
                       STRINGIFY(DEFAULT_NNUE_DIRECTORY)
#endif
    };

    if (netFile.empty())
        netFile = evalFile.defaultName;

    for (const auto& directory : dirs)
        if (netFile != std::string(evalFile.currentName))
        {
            if (directory != "<internal>")
            {
                load_user_net(directory, netFile);
            }
            else if (netFile == std::string(evalFile.defaultName))
            {
                load_internal();
            }
        }
}

template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::save(const std::optional<std::string>& netFile) const noexcept {
    std::string evalFileName;

    if (netFile.has_value())
        evalFileName = netFile.value();
    else
    {
        if (std::string(evalFile.currentName) != std::string(evalFile.defaultName))
        {
            UCI::print_info_string(
              "Failed to export net. Non-embedded net can only be saved if the filename is specified");
            return false;
        }

        evalFileName = evalFile.defaultName;
    }

    std::ofstream ofs(evalFileName, std::ios::binary);

    bool saved = save(ofs, evalFile.currentName, evalFile.netDescription);

    UCI::print_info_string(saved ? "Network saved successfully to " + evalFileName
                                 : "Failed to export net");
    return saved;
}

template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::verify(std::string netFile) const noexcept {
    if (netFile.empty())
        netFile = evalFile.defaultName;

    if (netFile != std::string(evalFile.currentName))
    {
        std::string msg1 =
          "Network evaluation parameters compatible with the engine must be available.";
        std::string msg2 = "The network file " + netFile + " was not loaded successfully.";
        std::string msg3 = "The UCI option EvalFile might need to specify the full path, "
                           "including the directory name, to the network file.";
        std::string msg4 = "The default net can be downloaded from: "
                           "https://tests.stockfishchess.org/api/nn/"
                         + std::string(evalFile.defaultName);
        std::string msg5 = "The engine will be terminated now.";

        std::cerr << "ERROR: " << msg1 << '\n'  //
                  << "ERROR: " << msg2 << '\n'  //
                  << "ERROR: " << msg3 << '\n'  //
                  << "ERROR: " << msg4 << '\n'  //
                  << "ERROR: " << msg5 << '\n'
                  << std::endl;
        std::exit(EXIT_FAILURE);
    }

    std::size_t size = sizeof(featureTransformer) + LayerStacks * sizeof(Arch);

    std::string msg = "NNUE evaluation using " + netFile + " ("
                    + std::to_string(size / (1024 * 1024)) + "MiB, ("
                    + std::to_string(featureTransformer.TotalInputDimensions) + ", "
                    + std::to_string(network[0].TransformedFeatureDimensions) + ", "
                    + std::to_string(network[0].FC_0_Outputs) + ", "  //
                    + std::to_string(network[0].FC_1_Outputs) + ", 1))";
    UCI::print_info_string(msg);
}

template<typename Arch, typename Transformer>
std::size_t Network<Arch, Transformer>::content_hash() const noexcept {
    if (!initialized)
        return 0;

    std::size_t h = 0;
    combine_hash(h, featureTransformer);
    for (auto&& net : network)
        combine_hash(h, net);
    combine_hash(h, evalFile);
    combine_hash(h, embeddedType);
    return h;
}

template<typename Arch, typename Transformer>
NetworkOutput
Network<Arch, Transformer>::evaluate(const Position&                         pos,
                                     AccumulatorStack&                       accStack,
                                     AccumulatorCaches::Cache<TFDimensions>& cache) const noexcept {

    alignas(CACHE_LINE_SIZE)
      StdArray<TransformedFeatureType, FeatureTransformer<TFDimensions>::BufferSize>
        transformedFeatures;

    auto bucket = pos.bucket();

    auto psqt = featureTransformer.transform(pos, accStack, cache, bucket, transformedFeatures);
    auto positional = network[bucket].propagate(transformedFeatures);

    return {psqt / OUTPUT_SCALE, positional / OUTPUT_SCALE};
}

template<typename Arch, typename Transformer>
NetworkTrace
Network<Arch, Transformer>::trace(const Position&                         pos,
                                  AccumulatorStack&                       accStack,
                                  AccumulatorCaches::Cache<TFDimensions>& cache) const noexcept {

    alignas(CACHE_LINE_SIZE)
      StdArray<TransformedFeatureType, FeatureTransformer<TFDimensions>::BufferSize>
        transformedFeatures;

    NetworkTrace netTrace;
    netTrace.correctBucket = pos.bucket();
    for (IndexType bucket = 0; bucket < LayerStacks; ++bucket)
    {
        auto psqt = featureTransformer.transform(pos, accStack, cache, bucket, transformedFeatures);
        auto positional = network[bucket].propagate(transformedFeatures);

        netTrace.netOut[bucket] = {psqt / OUTPUT_SCALE, positional / OUTPUT_SCALE};
    }

    return netTrace;
}

template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::load_user_net(const std::string& dir,
                                               const std::string& netFile) noexcept {
    std::ifstream ifs(dir + netFile, std::ios::binary);

    auto description = load(ifs);

    if (description.has_value())
    {
        evalFile.currentName    = netFile;
        evalFile.netDescription = description.value();
    }
}

template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::load_internal() noexcept {
    const auto embedded = get_embedded(embeddedType);

    MemoryStreamBuf buf(const_cast<char*>(reinterpret_cast<const char*>(embedded.data)),
                        std::size_t(embedded.size));

    std::istream is(&buf);

    auto description = load(is);

    if (description.has_value())
    {
        evalFile.currentName    = evalFile.defaultName;
        evalFile.netDescription = description.value();
    }
}

template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::initialize() noexcept {
    initialized = true;
}

template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::save(std::ostream&      os,
                                      const std::string& name,
                                      const std::string& netDescription) const noexcept {
    if (name.empty() || name == "None")
        return false;

    return write_parameters(os, netDescription);
}

template<typename Arch, typename Transformer>
std::optional<std::string> Network<Arch, Transformer>::load(std::istream& is) noexcept {
    initialize();

    std::string description;
    return read_parameters(is, description) ? std::make_optional(description) : std::nullopt;
}

template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::read_parameters(std::istream& is,
                                                 std::string&  netDescription) noexcept {
    std::uint32_t hash;
    if (!_read_header(is, hash, netDescription))
        return false;
    if (hash != Network::Hash)
        return false;
    if (!_read_parameters(is, featureTransformer))
        return false;
    for (std::size_t i = 0; i < LayerStacks; ++i)
        if (!_read_parameters(is, network[i]))
            return false;

    return bool(is) && is.peek() == std::ios::traits_type::eof();
}

template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::write_parameters(
  std::ostream& os, const std::string& netDescription) const noexcept {
    if (!_write_header(os, Network::Hash, netDescription))
        return false;
    if (!_write_parameters(os, featureTransformer))
        return false;
    for (std::size_t i = 0; i < LayerStacks; ++i)
        if (!_write_parameters(os, network[i]))
            return false;

    return bool(os);
}

// Explicit template instantiations:
template class Network<NetworkArchitecture<BigTransformedFeatureDimensions, BigL2, BigL3>,
                       FeatureTransformer<BigTransformedFeatureDimensions>>;
template class Network<NetworkArchitecture<SmallTransformedFeatureDimensions, SmallL2, SmallL3>,
                       FeatureTransformer<SmallTransformedFeatureDimensions>>;

}  // namespace DON::NNUE
