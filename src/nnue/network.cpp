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
#include <memory>
#include <type_traits>

#define INCBIN_SILENCE_BITCODE_WARNING
#include "../incbin/incbin.h"

#include "../evaluate.h"
#include "../memory.h"
#include "../misc.h"
#include "../position.h"
#include "../types.h"
#include "../uci.h"
#include "nnue_common.h"

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

struct EmbeddedNNUE final {
    EmbeddedNNUE(const unsigned char*       embeddedData,
                 const unsigned char* const embeddedEnd,
                 const unsigned int         embeddedSize) noexcept :
        data(embeddedData),
        end(embeddedEnd),
        size(embeddedSize) {}
    const unsigned char*       data;
    const unsigned char* const end;
    const unsigned int         size;
};

inline EmbeddedNNUE get_embedded(EmbeddedType embType) noexcept {
    assert(embType == BIG || embType == SMALL);
    return embType == BIG ? EmbeddedNNUE(gBigEmbeddedData, gBigEmbeddedEnd, gBigEmbeddedSize)
                          : EmbeddedNNUE(gSmallEmbeddedData, gSmallEmbeddedEnd, gSmallEmbeddedSize);
}

namespace Impl {

// Read network header
inline bool read_header(std::istream&  istream,  //
                        std::uint32_t& hashValue,
                        std::string&   netDescription) noexcept {
    std::uint32_t fileVersion, descSize;
    fileVersion = read_little_endian<std::uint32_t>(istream);
    hashValue   = read_little_endian<std::uint32_t>(istream);
    descSize    = read_little_endian<std::uint32_t>(istream);
    if (!istream || fileVersion != FILE_VERSION)
        return false;
    netDescription.resize(descSize);
    istream.read(&netDescription[0], descSize);

    return !istream.fail();
}

// Write network header
inline bool write_header(std::ostream&      ostream,  //
                         std::uint32_t      hashValue,
                         const std::string& netDescription) noexcept {
    write_little_endian<std::uint32_t>(ostream, FILE_VERSION);
    write_little_endian<std::uint32_t>(ostream, hashValue);
    write_little_endian<std::uint32_t>(ostream, netDescription.size());
    ostream.write(&netDescription[0], netDescription.size());

    return !ostream.fail();
}

// Read evaluation function parameters
template<typename T>
inline bool read_parameters(std::istream& istream, T& reference) noexcept {
    std::uint32_t hashValue;
    hashValue = read_little_endian<std::uint32_t>(istream);
    if (!istream || hashValue != T::hash_value())
        return false;

    return reference.read_parameters(istream);
}

// Write evaluation function parameters
template<typename T>
inline bool write_parameters(std::ostream& ostream, T& reference) noexcept {
    write_little_endian<std::uint32_t>(ostream, T::hash_value());

    return reference.write_parameters(ostream);
}

}  // namespace Impl

template<typename Arch, typename Transformer>
Network<Arch, Transformer>::Network(const Network<Arch, Transformer>& net) noexcept :
    evalFile(net.evalFile),
    embeddedType(net.embeddedType) {

    if (net.featureTransformer)
        featureTransformer = make_unique_aligned_lp<Transformer>(*net.featureTransformer);

    network = make_unique_aligned_std<Arch[]>(LayerStacks);
    if (net.network)
        for (std::size_t i = 0; i < LayerStacks; ++i)
            network[i] = net.network[i];
}

template<typename Arch, typename Transformer>
Network<Arch, Transformer>&
Network<Arch, Transformer>::operator=(const Network<Arch, Transformer>& net) noexcept {
    evalFile     = net.evalFile;
    embeddedType = net.embeddedType;

    if (net.featureTransformer)
        featureTransformer = make_unique_aligned_lp<Transformer>(*net.featureTransformer);

    network = make_unique_aligned_std<Arch[]>(LayerStacks);
    if (net.network)
        for (std::size_t i = 0; i < LayerStacks; ++i)
            network[i] = net.network[i];

    return *this;
}

template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::load(std::string_view rootDirectory,
                                      std::string      evalFileName) noexcept {

    const Strings dirs{"<internal>", "", std::string(rootDirectory)
#if defined(DEFAULT_NNUE_DIRECTORY)
                                           ,
                       STRINGIFY(DEFAULT_NNUE_DIRECTORY)
#endif
    };

    if (evalFileName.empty())
        evalFileName = evalFile.defaultName;

    for (const auto& directory : dirs)
        if (evalFileName != evalFile.current)
        {
            if (directory != "<internal>")
            {
                load_user_net(directory, evalFileName);
            }
            else if (evalFileName == evalFile.defaultName)
            {
                load_internal();
            }
        }
}

template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::save(const std::optional<std::string>& fileName) const noexcept {
    std::string evalFileName;

    if (fileName.has_value())
        evalFileName = fileName.value();
    else
    {
        if (evalFile.current != evalFile.defaultName)
        {
            UCI::print_info_string(
              "Failed to export net. Non-embedded net can only be saved if the filename is specified");
            return false;
        }

        evalFileName = evalFile.defaultName;
    }

    std::ofstream ofstream(evalFileName, std::ios_base::binary);

    bool saved = save(ofstream, evalFile.current, evalFile.netDescription);

    UCI::print_info_string(saved ? "Network saved successfully to " + evalFileName
                                 : "Failed to export net");
    return saved;
}

template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::verify(std::string evalFileName) const noexcept {
    if (evalFileName.empty())
        evalFileName = evalFile.defaultName;

    if (evalFileName != evalFile.current)
    {
        std::string msg1 =
          "Network evaluation parameters compatible with the engine must be available.";
        std::string msg2 = "The network file " + evalFileName + " was not loaded successfully.";
        std::string msg3 = "The UCI option EvalFile might need to specify the full path, "
                           "including the directory name, to the network file.";
        std::string msg4 = "The default net can be downloaded from: "
                           "https://tests.stockfishchess.org/api/nn/"
                         + evalFile.defaultName;
        std::string msg5 = "The engine will be terminated now.";

        std::string msg = "ERROR: " + msg1 + '\n'  //
                        + "ERROR: " + msg2 + '\n'  //
                        + "ERROR: " + msg3 + '\n'  //
                        + "ERROR: " + msg4 + '\n'  //
                        + "ERROR: " + msg5 + '\n';
        UCI::print_info_string(msg);
        std::exit(EXIT_FAILURE);
    }

    auto size = sizeof(*featureTransformer) + LayerStacks * sizeof(Arch);

    std::string msg = "NNUE evaluation using " + evalFileName + " ("
                    + std::to_string(size / (1024 * 1024)) + "MiB, ("
                    + std::to_string(featureTransformer->InputDimensions) + ", "
                    + std::to_string(network[0].TransformedFeatureDimensions) + ", "
                    + std::to_string(network[0].FC_0_Outputs) + ", "  //
                    + std::to_string(network[0].FC_1_Outputs) + ", 1))";
    UCI::print_info_string(msg);
}

template<typename Arch, typename Transformer>
NetworkOutput Network<Arch, Transformer>::evaluate(
  const Position&                                         pos,
  AccumulatorStack&                                       accStack,
  AccumulatorCaches::Cache<TransformedFeatureDimensions>* cache) const noexcept {

    alignas(CACHE_LINE_SIZE) TransformedFeatureType
      transformedFeatures[FeatureTransformer<TransformedFeatureDimensions>::BufferSize];

    ASSERT_ALIGNED(transformedFeatures, CACHE_LINE_SIZE);

    auto bucket = pos.bucket();

    auto psqt = featureTransformer->transform(pos, accStack, cache, bucket, transformedFeatures);
    auto positional = network[bucket].propagate(transformedFeatures);

    return {psqt / OUTPUT_SCALE, positional / OUTPUT_SCALE};
}

template<typename Arch, typename Transformer>
NetworkTrace Network<Arch, Transformer>::trace(
  const Position&                                         pos,
  AccumulatorStack&                                       accStack,
  AccumulatorCaches::Cache<TransformedFeatureDimensions>* cache) const noexcept {

    alignas(CACHE_LINE_SIZE) TransformedFeatureType
      transformedFeatures[FeatureTransformer<TransformedFeatureDimensions>::BufferSize];

    ASSERT_ALIGNED(transformedFeatures, CACHE_LINE_SIZE);

    NetworkTrace netTrace{};
    netTrace.correctBucket = pos.bucket();
    for (IndexType bucket = 0; bucket < LayerStacks; ++bucket)
    {
        auto psqt =
          featureTransformer->transform(pos, accStack, cache, bucket, transformedFeatures);
        auto positional = network[bucket].propagate(transformedFeatures);

        netTrace.netOut[bucket] = {psqt / OUTPUT_SCALE, positional / OUTPUT_SCALE};
    }

    return netTrace;
}

template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::load_user_net(const std::string& dir,
                                               const std::string& evalFileName) noexcept {
    std::ifstream ifstream(dir + evalFileName, std::ios_base::binary);

    auto description = load(ifstream);
    if (description.has_value())
    {
        evalFile.current        = evalFileName;
        evalFile.netDescription = description.value();
    }
}

template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::load_internal() noexcept {
    // C++ way to prepare a buffer for a memory stream
    class MemoryBuffer final: public std::streambuf {
       public:
        MemoryBuffer(char* p, std::size_t n) noexcept {
            setg(p, p, p + n);
            setp(p, p + n);
        }
    };

    const auto embedded = get_embedded(embeddedType);

    MemoryBuffer buffer(const_cast<char*>(reinterpret_cast<const char*>(embedded.data)),
                        std::size_t(embedded.size));

    std::istream istream(&buffer);

    auto description = load(istream);
    if (description.has_value())
    {
        evalFile.current        = evalFile.defaultName;
        evalFile.netDescription = description.value();
    }
}

template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::initialize() noexcept {
    featureTransformer = make_unique_aligned_lp<Transformer>();
    network            = make_unique_aligned_std<Arch[]>(LayerStacks);
}

template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::save(std::ostream&      ostream,
                                      const std::string& name,
                                      const std::string& netDescription) const noexcept {
    if (name.empty() || name == "None")
        return false;

    return write_parameters(ostream, netDescription);
}

template<typename Arch, typename Transformer>
std::optional<std::string> Network<Arch, Transformer>::load(std::istream& istream) noexcept {
    initialize();

    std::string description;
    return read_parameters(istream, description) ? std::make_optional(description) : std::nullopt;
}

template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::read_parameters(std::istream& istream,
                                                 std::string&  netDescription) noexcept {
    std::uint32_t hashValue;
    if (!Impl::read_header(istream, hashValue, netDescription))
        return false;
    if (hashValue != Network::HashValue)
        return false;
    if (!Impl::read_parameters(istream, *featureTransformer))
        return false;
    for (std::size_t i = 0; i < LayerStacks; ++i)
        if (!Impl::read_parameters(istream, network[i]))
            return false;

    return bool(istream) && istream.peek() == std::ios::traits_type::eof();
}

template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::write_parameters(
  std::ostream& ostream, const std::string& netDescription) const noexcept {
    if (!Impl::write_header(ostream, Network::HashValue, netDescription))
        return false;
    if (!Impl::write_parameters(ostream, *featureTransformer))
        return false;
    for (std::size_t i = 0; i < LayerStacks; ++i)
        if (!Impl::write_parameters(ostream, network[i]))
            return false;

    return bool(ostream);
}

// Explicit template instantiations
template class Network<  //
  NetworkArchitecture<BigTransformedFeatureDimensions, BigL2, BigL3>,
  FeatureTransformer<BigTransformedFeatureDimensions>>;
template class Network<  //
  NetworkArchitecture<SmallTransformedFeatureDimensions, SmallL2, SmallL3>,
  FeatureTransformer<SmallTransformedFeatureDimensions>>;

}  // namespace DON::NNUE
