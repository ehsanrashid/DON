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

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <type_traits>
#include <vector>

#include "../evaluate.h"
#include "../incbin/incbin.h"
#include "nnue_architecture.h"
#include "nnue_common.h"
#include "nnue_misc.h"

namespace DON {

namespace Eval::NNUE {

namespace {
// Macro to embed the default efficiently updatable neural network (NNUE) file
// data in the engine binary (using incbin.h, by Dale Weiler).
// This macro invocation will declare the following three variables
//     const unsigned char        gEmbeddedNNUEData[];  // a pointer to the embedded data
//     const unsigned char *const gEmbeddedNNUEEnd;     // a marker to the end
//     const unsigned int         gEmbeddedNNUESize;    // the size of the embedded file
// Note that this does not work in Microsoft Visual Studio.
#if !defined(_MSC_VER) && !defined(NNUE_EMBEDDING_OFF)
INCBIN(EmbeddedNNUEBig, EvalFileDefaultNameBig);
INCBIN(EmbeddedNNUESmall, EvalFileDefaultNameSmall);
#else
const unsigned char        gEmbeddedNNUEBigData[1]   = {0x0};
const unsigned char* const gEmbeddedNNUEBigEnd       = &gEmbeddedNNUEBigData[1];
const unsigned int         gEmbeddedNNUEBigSize      = 1;
const unsigned char        gEmbeddedNNUESmallData[1] = {0x0};
const unsigned char* const gEmbeddedNNUESmallEnd     = &gEmbeddedNNUESmallData[1];
const unsigned int         gEmbeddedNNUESmallSize    = 1;
#endif

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

EmbeddedNNUE get_embedded(Eval::NNUE::EmbeddedNNUEType type) noexcept {
    return type == Eval::NNUE::EmbeddedNNUEType::BIG
           ? EmbeddedNNUE(gEmbeddedNNUEBigData, gEmbeddedNNUEBigEnd, gEmbeddedNNUEBigSize)
           : EmbeddedNNUE(gEmbeddedNNUESmallData, gEmbeddedNNUESmallEnd, gEmbeddedNNUESmallSize);
}

}  // namespace

namespace Detail {

// Initialize the evaluation function parameters
template<typename T>
void initialize(AlignedPtr<T>& pointer) noexcept {

    pointer.reset(reinterpret_cast<T*>(std_aligned_alloc(alignof(T), sizeof(T))));
    std::memset(pointer.get(), 0, sizeof(T));
}

template<typename T>
void initialize(LargePagePtr<T>& pointer) noexcept {

    static_assert(alignof(T) <= 4096,
                  "aligned_large_pages_alloc() may fail for such a big alignment requirement of T");
    pointer.reset(reinterpret_cast<T*>(aligned_large_pages_alloc(sizeof(T))));
    std::memset(pointer.get(), 0, sizeof(T));
}

// Read evaluation function parameters
template<typename T>
bool read_parameters(std::istream& istream, T& reference) noexcept {

    std::uint32_t header;
    header = read_little_endian<std::uint32_t>(istream);
    if (!istream || header != T::get_hash_value())
        return false;
    return reference.read_parameters(istream);
}

// Write evaluation function parameters
template<typename T>
bool write_parameters(std::ostream& ostream, const T& reference) noexcept {

    write_little_endian<std::uint32_t>(ostream, T::get_hash_value());
    return reference.write_parameters(ostream);
}

}  // namespace Detail

template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::load(const std::string& rootDirectory,
                                      std::string        evalfilePath) noexcept {

    const std::vector<std::string> dirs {
        "<internal>", "", rootDirectory
#if defined(DEFAULT_NNUE_DIRECTORY)
          ,
          STRINGIFY(DEFAULT_NNUE_DIRECTORY)
#endif
    };

    if (evalfilePath.empty())
        evalfilePath = evalFile.defaultName;

    for (const std::string& directory : dirs)
    {
        if (evalFile.current != evalfilePath)
        {
            if (directory != "<internal>")
            {
                load_user_net(directory, evalfilePath);
            }

            if (directory == "<internal>" && evalfilePath == evalFile.defaultName)
            {
                load_internal();
            }
        }
    }
}

template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::save(const std::optional<std::string>& filename) const noexcept {
    std::string actualFilename;
    std::string msg;

    if (filename.has_value())
        actualFilename = filename.value();
    else
    {
        if (evalFile.current != evalFile.defaultName)
        {
            msg = "Failed to export a net. "
                  "A non-embedded net can only be saved if the filename is specified";

            sync_cout << msg << sync_endl;
            return false;
        }

        actualFilename = evalFile.defaultName;
    }

    std::ofstream ofstream(actualFilename, std::ios_base::binary);
    bool          saved = save(ofstream, evalFile.current, evalFile.netDescription);

    msg = saved ? "Network saved successfully to " + actualFilename : "Failed to export a net";

    sync_cout << msg << sync_endl;
    return saved;
}

template<typename Arch, typename Transformer>
Value Network<Arch, Transformer>::evaluate(const Position&                         pos,
                                           AccumulatorCaches::Cache<FTDimensions>* cache,
                                           bool                                    adjusted,
                                           int* complexity) const noexcept {
    // We manually align the arrays on the stack because with gcc < 9.3
    // overaligning stack variables with alignas() doesn't work correctly.

    constexpr std::uint64_t Alignment = CacheLineSize;

#if defined(ALIGNAS_ON_STACK_VARIABLES_BROKEN)
    TransformedFeatureType
      transformedFeaturesUnaligned[FeatureTransformer<FTDimensions, nullptr>::BufferSize
                                   + Alignment / sizeof(TransformedFeatureType)];

    auto* transformedFeatures = align_ptr_up<Alignment>(&transformedFeaturesUnaligned[0]);
#else
    alignas(Alignment) TransformedFeatureType
      transformedFeatures[FeatureTransformer<FTDimensions, nullptr>::BufferSize];
#endif

    ASSERT_ALIGNED(transformedFeatures, Alignment);

    int  bucket     = (pos.count<ALL_PIECE>() - 1) / 4;
    auto psqt       = featureTransformer->transform(pos, cache, transformedFeatures, bucket);
    auto positional = network[bucket]->propagate(transformedFeatures);

    if (complexity)
        *complexity = std::abs(psqt - positional) / OutputScale;

    int delta = 24 * adjusted;
    // Give more value to positional evaluation when adjusted flag is set
    return ((1024 - delta) * psqt + (1024 + delta) * positional) / (1024 * OutputScale);
}

template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::verify(std::string evalfilePath) const noexcept {
    if (evalfilePath.empty())
        evalfilePath = evalFile.defaultName;

    if (evalFile.current != evalfilePath)
    {
        std::string msg1 =
          "Network evaluation parameters compatible with the engine must be available.";
        std::string msg2 = "The network file " + evalfilePath + " was not loaded successfully.";
        std::string msg3 = "The UCI option EvalFile might need to specify the full path, "
                           "including the directory name, to the network file.";
        std::string msg4 = "The default net can be downloaded from: "
                           "https://tests.stockfishchess.org/api/nn/"
                         + evalFile.defaultName;
        std::string msg5 = "The engine will be terminated now.";

        sync_cout << "info string ERROR: " << msg1 << sync_endl;
        sync_cout << "info string ERROR: " << msg2 << sync_endl;
        sync_cout << "info string ERROR: " << msg3 << sync_endl;
        sync_cout << "info string ERROR: " << msg4 << sync_endl;
        sync_cout << "info string ERROR: " << msg5 << sync_endl;
        exit(EXIT_FAILURE);
    }

    size_t size = sizeof(*featureTransformer) + sizeof(*network) * LayerStacks;
    sync_cout << "info string NNUE evaluation using " << evalfilePath << " ("
              << size / (1024 * 1024) << "MiB, (" << featureTransformer->InputDimensions << ", "
              << network[0]->TransformedFeatureDimensions << ", " << network[0]->FC_0_OUTPUTS
              << ", " << network[0]->FC_1_OUTPUTS << ", 1))" << sync_endl;
}

template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::hint_common_access(
  const Position& pos, AccumulatorCaches::Cache<FTDimensions>* cache) const noexcept {
    featureTransformer->hint_common_access(pos, cache);
}

template<typename Arch, typename Transformer>
NnueEvalTrace Network<Arch, Transformer>::trace_evaluate(
  const Position& pos, AccumulatorCaches::Cache<FTDimensions>* cache) const noexcept {
    // We manually align the arrays on the stack because with gcc < 9.3
    // overaligning stack variables with alignas() doesn't work correctly.
    constexpr std::uint64_t Alignment = CacheLineSize;

#if defined(ALIGNAS_ON_STACK_VARIABLES_BROKEN)
    TransformedFeatureType
      transformedFeaturesUnaligned[FeatureTransformer<FTDimensions, nullptr>::BufferSize
                                   + Alignment / sizeof(TransformedFeatureType)];

    auto* transformedFeatures = align_ptr_up<Alignment>(&transformedFeaturesUnaligned[0]);
#else
    alignas(Alignment) TransformedFeatureType
      transformedFeatures[FeatureTransformer<FTDimensions, nullptr>::BufferSize];
#endif

    ASSERT_ALIGNED(transformedFeatures, Alignment);

    NnueEvalTrace trace{};
    trace.correctBucket = (pos.count<ALL_PIECE>() - 1) / 4;
    for (IndexType bucket = 0; bucket < LayerStacks; ++bucket)
    {
        auto materialist = featureTransformer->transform(pos, cache, transformedFeatures, bucket);
        auto positional  = network[bucket]->propagate(transformedFeatures);

        trace.psqt[bucket]       = materialist / OutputScale;
        trace.positional[bucket] = positional / OutputScale;
    }

    return trace;
}

template<typename Arch, typename Transformer>
void Network<Arch, Transformer>::load_user_net(const std::string& dir,
                                               const std::string& evalfilePath) noexcept {
    std::ifstream ifstream(dir + evalfilePath, std::ios_base::binary);
    auto          description = load(ifstream);

    if (description.has_value())
    {
        evalFile.current        = evalfilePath;
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
    Detail::initialize(featureTransformer);
    for (auto& net : network)
        Detail::initialize(net);
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

// Read network header
template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::read_header(std::istream&  istream,
                                             std::uint32_t* hashValue,
                                             std::string*   desc) const noexcept {
    std::uint32_t version, size;

    version    = read_little_endian<std::uint32_t>(istream);
    *hashValue = read_little_endian<std::uint32_t>(istream);
    size       = read_little_endian<std::uint32_t>(istream);
    if (!istream || version != Version)
        return false;
    desc->resize(size);
    istream.read(&(*desc)[0], size);
    return !istream.fail();
}

// Write network header
template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::write_header(std::ostream&      ostream,
                                              std::uint32_t      hashValue,
                                              const std::string& desc) const noexcept {
    write_little_endian<std::uint32_t>(ostream, Version);
    write_little_endian<std::uint32_t>(ostream, hashValue);
    write_little_endian<std::uint32_t>(ostream, std::uint32_t(desc.size()));
    ostream.write(&desc[0], desc.size());
    return !ostream.fail();
}

template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::read_parameters(std::istream& istream,
                                                 std::string&  netDescription) const noexcept {
    std::uint32_t hashValue;
    if (!read_header(istream, &hashValue, &netDescription))
        return false;
    if (hashValue != Network::Hash)
        return false;
    if (!Detail::read_parameters(istream, *featureTransformer))
        return false;
    for (auto& net : network)
    {
        if (!Detail::read_parameters(istream, *net))
            return false;
    }
    return bool(istream) && istream.peek() == std::ios::traits_type::eof();
}

template<typename Arch, typename Transformer>
bool Network<Arch, Transformer>::write_parameters(
  std::ostream& ostream, const std::string& netDescription) const noexcept {
    if (!write_header(ostream, Network::Hash, netDescription))
        return false;
    if (!Detail::write_parameters(ostream, *featureTransformer))
        return false;
    for (const auto& net : network)
    {
        if (!Detail::write_parameters(ostream, *net))
            return false;
    }
    return bool(ostream);
}

// Explicit template instantiation
template class Network<
  NetworkArchitecture<BigTransformedFeatureDimensions, BigL2, BigL3>,
  FeatureTransformer<BigTransformedFeatureDimensions, &StateInfo::bigAccumulator>>;
template class Network<
  NetworkArchitecture<SmallTransformedFeatureDimensions, SmallL2, SmallL3>,
  FeatureTransformer<SmallTransformedFeatureDimensions, &StateInfo::smallAccumulator>>;

}  // namespace Eval::NNUE
}  // namespace DON
