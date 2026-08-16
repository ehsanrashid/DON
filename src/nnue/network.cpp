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

#include "network.h"

#include <cstdlib>
#include <fstream>
#include <iostream>

#define INCBIN_SILENCE_BITCODE_WARNING
#include "../incbin/incbin.h"

#include "../evaluate.h"
#include "../misc.h"
#include "../position.h"
#include "../notation.h"
#include "../types.h"
#include "common.h"

// Determined at runtime, see universal/nnue_embed.cpp
#if defined(UNIVERSAL_BINARY)
    #if defined(UNIVERSAL_BINARY_MACOS_X86_SLICE)
extern const unsigned char* const gEmbeddedNNUEData;
extern const unsigned int         gEmbeddedNNUESize;
    #else
extern const unsigned char gEmbeddedNNUEData[];
extern const unsigned int  gEmbeddedNNUESize;
    #endif
// Note that this does not work in Microsoft Visual Studio.
#elif !defined(NO_NNUE_EMBEDDING) && !defined(_MSC_VER)
// Macro to embed the default efficiently updatable neural network (NNUE) file
// data in the engine binary (using incbin.h, by Dale Weiler).
// This macro invocation will declare the following three variables
//     const unsigned char        gEmbeddedNNUEData[];  // pointer to the embedded data
//     const unsigned char *const gEmbeddedNNUEEnd;     // marker to the embedded end
//     const unsigned int         gEmbeddedNNUESize;    // size of the embedded file
INCBIN(EmbeddedNNUE, EvalFileDefaultName);
#else
const unsigned char gEmbeddedNNUEData[1] = {0x0};
const unsigned int  gEmbeddedNNUESize    = 1;
#endif

namespace DON::NNUE {

namespace {

// Read network header
bool _read_header(std::istream& is, u32& hash, std::string& netDescription) noexcept {
    u32 fileVersion, descSize;
    fileVersion = read_little_endian<u32>(is);
    hash        = read_little_endian<u32>(is);
    descSize    = read_little_endian<u32>(is);

    if (!is || fileVersion != FILE_VERSION)
        return false;

    netDescription.resize(descSize);
    is.read(netDescription.data(), descSize);

    return !is.fail();
}

// Write network header
bool _write_header(std::ostream& os, u32 hash, std::string_view netDescription) noexcept {
    write_little_endian<u32>(os, FILE_VERSION);
    write_little_endian<u32>(os, hash);
    write_little_endian<u32>(os, netDescription.size());
    os.write(netDescription.data(), netDescription.size());

    return !os.fail();
}

// Read evaluation function parameters
template<typename T>
bool _read_parameters(std::istream& is, T& reference) noexcept {
    u32 hash;
    hash = read_little_endian<u32>(is);

    if (!is || hash != T::hash())
        return false;

    return reference.read_parameters(is);
}

// Write evaluation function parameters
template<typename T>
bool _write_parameters(std::ostream& os, const T& reference) noexcept {
    write_little_endian<u32>(os, T::hash());

    return reference.write_parameters(os);
}

}  // namespace

void Network::load(const std::filesystem::path& rootDirectory,
                   std::filesystem::path        evalFilePath,
                   EvalFile&                    evalFile) noexcept {

    constexpr usize DirectorySize =
#if defined(DEFAULT_NNUE_DIRECTORY)
      3
#else
      2
#endif
      ;

    const Array<std::filesystem::path, DirectorySize> Directories{
      // --------------------------------------------------------
      std::filesystem::path{},  //
      rootDirectory
#if defined(DEFAULT_NNUE_DIRECTORY)
      ,
      path_from_utf8(STRINGIFY(DEFAULT_NNUE_DIRECTORY))
#endif
    };

    if (evalFilePath.empty())
        evalFilePath = evalFile.DefaultName;

    initialized = false;

    if (evalFile.currentPath != evalFilePath && evalFilePath == evalFile.DefaultName)
    {
        load_embedded(evalFile);

        if (initialized)
            return;
    }

    for (const auto& dir : Directories)
        if (evalFile.currentPath != evalFilePath)
        {
            load_external(dir, evalFilePath, evalFile);

            if (initialized)
                return;
        }
}

bool Network::save(const std::optional<std::filesystem::path>& evalFilePath,
                   const EvalFile&                             evalFile) const noexcept {
    if (!evalFile.currentPath)
    {
        print_info_string(
          "Failed to export a net. No network file is currently loaded. Please load a network file first.");
        return false;
    }

    if (!evalFilePath && evalFile.currentPath != evalFile.DefaultName)
    {
        print_info_string(
          "Failed to export a net. A non-embedded net can only be saved if the filename is specified.");
        return false;
    }

    std::filesystem::path evalFileName = evalFilePath.value_or(evalFile.DefaultName);

    std::ofstream ofs{evalFileName, std::ios::binary};

    bool saved = save(ofs, evalFile.netDescription);

    print_info_string(saved ? "Network saved successfully to " + evalFileName.string() + "."
                            : "Failed to export net.");
    return saved;
}

void Network::verify(std::filesystem::path evalFilePath, const EvalFile& evalFile) const noexcept {
    if (evalFilePath.empty())
        evalFilePath = evalFile.DefaultName;

    if (evalFile.currentPath != evalFilePath)
    {
        std::string msg1{
          "Network evaluation parameters compatible with the engine must be available."};
        std::string msg2{"The network file " + evalFilePath.string()
                         + " was not loaded successfully."};
        std::string msg3{
          "The UCI option EvalFile might need to specify the full path, including the directory name, to the network file."};
        std::string msg4{
          "The default net can be downloaded from: https://tests.stockfishchess.org/api/nn/"
          + std::string{evalFile.DefaultName}};
        std::string msg5{"The engine will be terminated now."};

        std::cerr << "ERROR: " << msg1 << '\n'  //
                  << "ERROR: " << msg2 << '\n'  //
                  << "ERROR: " << msg3 << '\n'  //
                  << "ERROR: " << msg4 << '\n'  //
                  << "ERROR: " << msg5 << '\n'
                  << std::endl;
        std::exit(EXIT_FAILURE);
    }

    constexpr usize TotalSize =
      sizeof(featureTransformer) + LayerStacks * sizeof(NetworkArchitecture);

    std::string msg{"NNUE evaluation using " + evalFilePath.string() + " ("
                    + std::to_string(TotalSize / MB) + "MiB, ("
                    + std::to_string(featureTransformer.InputDimensions) + ", "
                    + std::to_string(networkArchitectures[0].TransformedFeatureDimensions) + ", "
                    + std::to_string(networkArchitectures[0].FC_0_Outputs) + ", "
                    + std::to_string(networkArchitectures[0].FC_1_Outputs) + ", 1))"};
    print_info_string(msg);
}

usize Network::content_hash() const noexcept {
    usize h = 0;
    if (initialized)
    {
        combine_hash(h, featureTransformer);
        for (auto&& arch : networkArchitectures)
            combine_hash(h, arch);
    }
    return h;
}

NetworkOutput Network::evaluate(const Position&   pos,
                                AccumulatorCache& accCache,
                                AccumulatorStack& accStack) const noexcept {
    constexpr usize Alignment = CACHE_LINE_SIZE;

    alignas(Alignment) Array<TransformedFeatureType, FeatureTransformer::BufferSize>
      transformedFeatures;

    ASSERT_ALIGNED(transformedFeatures.data(), Alignment);

    const auto bucket     = pos.bucket();
    const auto psqt       = featureTransformer.transform(pos, accCache, accStack,  //
                                                         bucket, transformedFeatures);
    const auto positional = networkArchitectures[bucket].propagate(transformedFeatures);

    return {psqt / OUTPUT_SCALE, positional / OUTPUT_SCALE};
}

NetworkTrace Network::trace(const Position&   pos,
                            AccumulatorCache& accCache,
                            AccumulatorStack& accStack) const noexcept {
    constexpr usize Alignment = CACHE_LINE_SIZE;

    alignas(Alignment) Array<TransformedFeatureType, FeatureTransformer::BufferSize>
      transformedFeatures;

    ASSERT_ALIGNED(transformedFeatures.data(), Alignment);

    NetworkTrace netTrace{};
    netTrace.correctBucket = pos.bucket();
    for (IndexType bucket = 0; bucket < LayerStacks; ++bucket)
    {
        const auto psqt       = featureTransformer.transform(pos, accCache, accStack,  //
                                                             bucket, transformedFeatures);
        const auto positional = networkArchitectures[bucket].propagate(transformedFeatures);

        netTrace.netOut[bucket] = {psqt / OUTPUT_SCALE, positional / OUTPUT_SCALE};
    }

    return netTrace;
}

bool Network::load_embedded(EvalFile& evalFile) noexcept {

#if defined(UNIVERSAL_BINARY_MACOS_X86_SLICE)
    if (gEmbeddedNNUEData == nullptr)  // failed embedded load
        return;
#endif

    MemoryStreamBuf buf(const_cast<char*>(reinterpret_cast<const char*>(gEmbeddedNNUEData)),
                        usize(gEmbeddedNNUESize));

    std::istream is{&buf};

    auto netDescription = load(is);

    if (netDescription)
    {
        evalFile.currentPath    = evalFile.DefaultName;
        evalFile.netDescription = *netDescription;
        return true;
    }

    return false;
}

bool Network::load_external(const std::filesystem::path& dir,
                            const std::filesystem::path& evalFilePath,
                            EvalFile&                    evalFile) noexcept {

    std::filesystem::path path = dir / evalFilePath;

    std::ifstream ifs{path, std::ios::binary};

    auto netDescription = load(ifs);

    if (netDescription)
    {
        evalFile.currentPath    = evalFilePath;
        evalFile.netDescription = *netDescription;
        return true;
    }

    return false;
}

std::optional<std::string> Network::load(std::istream& is) noexcept {

    std::string netDescription;
    if (!read_parameters(is, netDescription))
        return std::nullopt;

    initialized = true;

    return netDescription;
}

bool Network::save(std::ostream& os, std::string_view netDescription) const noexcept {
    return write_parameters(os, netDescription);
}

bool Network::read_parameters(std::istream& is, std::string& netDescription) noexcept {
    u32 hash;
    if (!_read_header(is, hash, netDescription))
        return false;

    if (hash != Network::Hash)
        return false;

    if (!_read_parameters(is, featureTransformer))
        return false;

    for (auto& arch : networkArchitectures)
        if (!_read_parameters(is, arch))
            return false;

    return bool(is) && is.peek() == std::ios::traits_type::eof();
}

bool Network::write_parameters(std::ostream& os, std::string_view netDescription) const noexcept {

    if (!_write_header(os, Network::Hash, netDescription))
        return false;

    if (!_write_parameters(os, featureTransformer))
        return false;

    for (const auto& arch : networkArchitectures)
        if (!_write_parameters(os, arch))
            return false;

    return bool(os);
}

}  // namespace DON::NNUE
