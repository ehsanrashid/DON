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

#include "misc.h"

#include <cmath>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <limits>

#if defined(_WIN32)
    #include <direct.h>
    #define GETCWD _getcwd
#else
    #include <unistd.h>
    #define GETCWD getcwd
#endif

namespace DON {

namespace {

constexpr std::string_view Name{"DON"};
constexpr std::string_view Author{"Ehsan Rashid"};
constexpr std::string_view Version{"1.0"};

// Format date to YYYYMMDD
[[maybe_unused]] std::string format_date(std::string_view date) noexcept {
    //constexpr std::string_view Months{"Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec"};
    constexpr StdArray<std::string_view, 12> Months{
      "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"  //
    };

    constexpr std::string_view NullDate{"00000000"};

    // Tokenize: expect "Mon DD YYYY" where DD may have a trailing comma.
    // Format from compiler: "Sep 02 2008"

    if (date.size() < 8)
        return std::string{NullDate};

    // Parse month (first 3 chars), then skip space(s), then day, then space, then year
    const char* p   = date.data();
    const char* end = p + date.size();

    // Parse month (first 3 chars)
    if (end - p < 3)
        return std::string{NullDate};

    std::string_view month{p, 3};
    p += 3;

    // Skip spaces
    while (p < end && std::isspace((unsigned char) (*p)))
        ++p;

    // Parse day (1-2 digits)
    if (end - p < 1 || !std::isdigit((unsigned char) (*p)))
        return std::string{NullDate};

    unsigned day = 0;
    while (p < end && std::isdigit((unsigned char) (*p)))
    {
        day *= 10;
        day += char_to_digit(*p);
        ++p;
    }

    // Validate day range
    if (day < 1 || day > 31)
        return std::string{NullDate};

    // Skip spaces/comma
    while (p < end && (std::isspace((unsigned char) (*p)) || *p == ','))
        ++p;

    // Parse year (4 digits)
    if (end - p < 4)
        return std::string{NullDate};

    unsigned year = 0;
    for (std::size_t i = 0; i < 4; ++i)
    {
        if (!std::isdigit((unsigned char) (p[i])))
            return std::string{NullDate};
        year *= 10;
        year += char_to_digit(p[i]);
    }

    // Validate year range (reasonable bounds)
    if (year < 1970)
        return std::string{NullDate};

    // Find month index (1..12)
    auto itr = std::find(Months.begin(), Months.end(), month);
    if (itr == Months.end())
        return std::string{NullDate};

    unsigned monthId = 1 +
                       //Months.find(month) / 4;
                       std::distance(Months.begin(), itr);

    // Format YYYYMMDD manually (faster than snprintf)
    StdArray<char, 9> buffer{};  // 8 chars + '\0'

    buffer[0] = digit_to_char(year / 1000 % 10);
    buffer[1] = digit_to_char(year / 100 % 10);
    buffer[2] = digit_to_char(year / 10 % 10);
    buffer[3] = digit_to_char(year % 10);
    buffer[4] = digit_to_char(monthId / 10 % 10);
    buffer[5] = digit_to_char(monthId % 10);
    buffer[6] = digit_to_char(day / 10 % 10);
    buffer[7] = digit_to_char(day % 10);
    buffer[8] = '\0';

    return std::string{buffer.data(), buffer.size() - 1};
}

}  // namespace

std::string engine_info(bool uci) noexcept {
    std::string engineInfo;
    engineInfo.reserve(64);

    if (uci)
        engineInfo = "id name ";
    engineInfo += version_info();
    engineInfo += uci ? "\nid author " : " by ";
    engineInfo += Author;

    return engineInfo;
}

// Returns the full name of the current DON version.
// For local dev compiles try to append the commit sha and commit date from git.
// If that fails only the local compilation date is set and "nogit" is specified:
//  - DON dev-YYYYMMDD-SHA
// or
//  - DON dev-YYYYMMDD-nogit
//
// For releases (non-dev builds) only include the version number:
//  - DON version
std::string version_info() noexcept {
    std::string versionInfo;
    versionInfo.reserve(32);

    versionInfo = Name;
    versionInfo += ' ';
    versionInfo += Version;

    if constexpr (Version == "dev")
    {
        versionInfo += '-';
#if defined(GIT_DATE)
        versionInfo += STRINGIFY(GIT_DATE);
#else
        versionInfo += format_date(__DATE__);
#endif
        versionInfo += '-';
#if defined(GIT_SHA)
        versionInfo += STRINGIFY(GIT_SHA);
#else
        versionInfo += "nogit";
#endif
    }

    return versionInfo;
}

// Returns a string trying to describe the compiler used
std::string compiler_info() noexcept {

#define VERSION_STRING(major, minor, patch) \
    STRINGIFY(major) "." STRINGIFY(minor) "." STRINGIFY(patch)

    // Predefined macros hell:
    //
    // __GNUC__                Compiler is GCC, Clang or ICX
    // __clang__               Compiler is Clang or ICX
    // __INTEL_LLVM_COMPILER   Compiler is ICX
    // _MSC_VER                Compiler is MSVC
    // _WIN32                  Building on Windows (any)
    // _WIN64                  Building on Windows 64 bit

    std::string compilerInfo;
    compilerInfo.reserve(256);

    compilerInfo = "\nCompiled by                : ";
#if defined(__INTEL_LLVM_COMPILER)
    compilerInfo += "ICX ";
    compilerInfo += STRINGIFY(__INTEL_LLVM_COMPILER);
#elif defined(__clang__)
    compilerInfo += "clang++ ";
    compilerInfo += VERSION_STRING(__clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
    compilerInfo += "g++ (GNUC) ";
    compilerInfo += VERSION_STRING(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    compilerInfo += "MSVC ";
    compilerInfo += STRINGIFY(_MSC_FULL_VER) "." STRINGIFY(_MSC_BUILD);
#elif defined(__e2k__) && defined(__LCC__)
    compilerInfo += "MCST LCC ";
    compilerInfo += std::to_string(__LCC__ / 100);
    compilerInfo += ".";
    compilerInfo += std::to_string(__LCC__ % 100);
    compilerInfo += ".";
    compilerInfo += std::to_string(__LCC_MINOR__);
#else
    compilerInfo += "(unknown compiler)";
#endif

    compilerInfo += "\nCompiled on                : ";
#if defined(__APPLE__)
    compilerInfo += "Apple";
#elif defined(__CYGWIN__)
    compilerInfo += "Cygwin";
#elif defined(__MINGW64__)
    compilerInfo += "MinGW64";
#elif defined(__MINGW32__)
    compilerInfo += "MinGW32";
#elif defined(__ANDROID__)
    compilerInfo += "Android";
#elif defined(__linux__)
    compilerInfo += "Linux";
#elif defined(_WIN64)
    compilerInfo += "Microsoft Windows 64-bit";
#elif defined(_WIN32)
    compilerInfo += "Microsoft Windows 32-bit";
#else
    compilerInfo += "(unknown system)";
#endif

    compilerInfo += "\nCompilation architecture   : ";
#if defined(ARCH)
    compilerInfo += STRINGIFY(ARCH);
#else
    compilerInfo += "(undefined architecture)";
#endif

    compilerInfo += "\nCompilation settings       : ";
#if defined(IS_64BIT)
    compilerInfo += "64-bit";
#else
    compilerInfo += "32-bit";
#endif
#if defined(USE_AVX512ICL)
    compilerInfo += " AVX512ICL";
#endif
#if defined(USE_VNNI)
    compilerInfo += " VNNI";
#endif
#if defined(USE_AVX512)
    compilerInfo += " AVX512";
#endif
#if defined(USE_BMI2)
    compilerInfo += " BMI2";
    #if defined(USE_COMP)
    compilerInfo += " COMP";
    #endif
#endif
#if defined(USE_AVX2)
    compilerInfo += " AVX2";
#endif
#if defined(USE_SSE41)
    compilerInfo += " SSE41";
#endif
#if defined(USE_SSSE3)
    compilerInfo += " SSSE3";
#endif
#if defined(USE_SSE2)
    compilerInfo += " SSE2";
#endif
#if defined(USE_NEON)
    #if defined(USE_NEON_DOTPROD)
    compilerInfo += " NEON_DOTPROD";
    #else
    compilerInfo += " NEON";
    #endif
#endif
#if defined(USE_POPCNT)
    compilerInfo += " POPCNT";
#endif

#if !defined(NDEBUG)
    compilerInfo += " DEBUG";
#endif

    compilerInfo += "\nCompiler __VERSION__ macro : ";
#if defined(__VERSION__)
    compilerInfo += __VERSION__;
#else
    compilerInfo += "(undefined macro)";
#endif

#undef VERSION_STRING

    return compilerInfo;
}

std::string format_time(const std::chrono::system_clock::time_point& timePoint) noexcept {
    // clang-format off
    std::time_t   time = std::chrono::system_clock::to_time_t(timePoint);
    std::uint64_t usec = std::chrono::duration_cast<std::chrono::microseconds>(timePoint.time_since_epoch()).count() % 1000000;

    std::tm tm{};
#if defined(_WIN32)  // Windows
    localtime_s(&tm, &time);
#elif defined(__unix__) || defined(__APPLE__)  // POSIX (Linux / macOS)
    localtime_r(&time, &tm);
#else
    // Fallback (not thread-safe)
    tm = *std::localtime(&time);
#endif

    StdArray<char, 32> buffer{};

    std::size_t writtenSize;
    // Format the YYYY.MM.DD-HH:MM:SS part
    writtenSize = std::strftime(buffer.data(), buffer.size(), "%Y.%m.%d-%H:%M:%S", &tm);
    // Append microseconds safely
    writtenSize += std::snprintf(buffer.data() + writtenSize, buffer.size() - writtenSize, ".%06" PRIu64, usec);
    // clang-format on
    return std::string{buffer.data(), std::min(writtenSize, buffer.size() - 1)};
}

#if !defined(NDEBUG)
// Debug functions used mainly to collect run-time statistics
namespace Debug {
namespace {

template<std::size_t Size>
class Info {
   public:
    Info() noexcept {
        for (std::size_t i = 0; i < Size; ++i)
            _data[i].store(0, std::memory_order_relaxed);
    }

    Info(const Info& info) noexcept {
        for (std::size_t i = 0; i < Size; ++i)
            _data[i].store(info._data[i].load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
    }
    Info& operator=(const Info& info) noexcept {
        if (this == &info)
            return *this;

        for (std::size_t i = 0; i < Size; ++i)
            _data[i].store(info._data[i].load(std::memory_order_relaxed),
                           std::memory_order_relaxed);
        return *this;
    }

    Info(Info&&) noexcept            = delete;
    Info& operator=(Info&&) noexcept = delete;

    [[nodiscard]] decltype(auto) operator[](std::size_t index) const noexcept {
        assert(index < Size && "Index out of bounds");
        return _data[index];
    }
    [[nodiscard]] decltype(auto) operator[](std::size_t index) noexcept {
        assert(index < Size && "Index out of bounds");
        return _data[index];
    }

   protected:
    StdArray<std::atomic<std::int64_t>, Size> _data;
};

class MinInfo final: public Info<2> {
   public:
    MinInfo() noexcept {
        _data[1].store(std::numeric_limits<std::int64_t>::max(), std::memory_order_relaxed);
    }
};

class MaxInfo final: public Info<2> {
   public:
    MaxInfo() noexcept {
        _data[1].store(std::numeric_limits<std::int64_t>::min(), std::memory_order_relaxed);
    }
};

class ExtremeInfo final: public Info<3> {
   public:
    ExtremeInfo() noexcept {
        _data[1].store(std::numeric_limits<std::int64_t>::max(), std::memory_order_relaxed);
        _data[2].store(std::numeric_limits<std::int64_t>::min(), std::memory_order_relaxed);
    }
};


constexpr std::size_t MAX_SLOT = 64;

StdArray<Info<2>, MAX_SLOT>     hit;
StdArray<MinInfo, MAX_SLOT>     min;
StdArray<MaxInfo, MAX_SLOT>     max;
StdArray<ExtremeInfo, MAX_SLOT> extreme;
StdArray<Info<2>, MAX_SLOT>     mean;
StdArray<Info<3>, MAX_SLOT>     stdev;
StdArray<Info<6>, MAX_SLOT>     correl;

}  // namespace

void clear() noexcept {
    hit.fill({});
    min.fill({});
    max.fill({});
    extreme.fill({});
    mean.fill({});
    stdev.fill({});
    correl.fill({});
}

void hit_on(bool cond, std::size_t slot) noexcept {
    assert(slot < hit.size());
    if (slot >= hit.size())
        return;
    auto& info = hit[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    if (cond)
        info[1].fetch_add(1, std::memory_order_relaxed);
}

void min_of(std::int64_t value, std::size_t slot) noexcept {
    assert(slot < min.size());
    if (slot >= min.size())
        return;
    auto& info = min[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    {
        auto& mn = info[1];
        for (auto minValue = mn.load(std::memory_order_relaxed);
             minValue > value
             && !mn.compare_exchange_weak(minValue, value,            //
                                          std::memory_order_relaxed,  //
                                          std::memory_order_relaxed);)
        {}
    }
}

void max_of(std::int64_t value, std::size_t slot) noexcept {
    assert(slot < max.size());
    if (slot >= max.size())
        return;
    auto& info = max[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    {
        auto& mx = info[1];
        for (auto maxValue = mx.load(std::memory_order_relaxed);
             maxValue < value
             && !mx.compare_exchange_weak(maxValue, value,            //
                                          std::memory_order_relaxed,  //
                                          std::memory_order_relaxed);)
        {}
    }
}

void extreme_of(std::int64_t value, std::size_t slot) noexcept {
    assert(slot < extreme.size());
    if (slot >= extreme.size())
        return;
    auto& info = extreme[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    {
        auto& mn = info[1];
        for (auto minValue = mn.load(std::memory_order_relaxed);
             minValue > value
             && !mn.compare_exchange_weak(minValue, value,            //
                                          std::memory_order_relaxed,  //
                                          std::memory_order_relaxed);)
        {}
    }
    {
        auto& mx = info[2];
        for (auto maxValue = mx.load(std::memory_order_relaxed);
             maxValue < value
             && !mx.compare_exchange_weak(maxValue, value,            //
                                          std::memory_order_relaxed,  //
                                          std::memory_order_relaxed);)
        {}
    }
}

void mean_of(std::int64_t value, std::size_t slot) noexcept {
    assert(slot < mean.size());
    if (slot >= mean.size())
        return;
    auto& info = mean[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    info[1].fetch_add(value, std::memory_order_relaxed);
}

void stdev_of(std::int64_t value, std::size_t slot) noexcept {
    assert(slot < stdev.size());
    if (slot >= stdev.size())
        return;
    auto& info = stdev[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    info[1].fetch_add(value, std::memory_order_relaxed);
    info[2].fetch_add(value * value, std::memory_order_relaxed);
}

void correl_of(std::int64_t value1, std::int64_t value2, std::size_t slot) noexcept {
    assert(slot < correl.size());
    if (slot >= correl.size())
        return;
    auto& info = correl[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    info[1].fetch_add(value1, std::memory_order_relaxed);
    info[2].fetch_add(value1 * value1, std::memory_order_relaxed);
    info[3].fetch_add(value2, std::memory_order_relaxed);
    info[4].fetch_add(value2 * value2, std::memory_order_relaxed);
    info[5].fetch_add(value1 * value2, std::memory_order_relaxed);
}

void print() noexcept {

    std::int64_t n;
    auto         avg = [&n](std::int64_t x) noexcept { return double(x) / n; };

    for (std::size_t i = 0; i < hit.size(); ++i)
    {
        auto& info = hit[i];

        if (!(n = info[0].load(std::memory_order_relaxed)))
            continue;

        auto hits = info[1].load(std::memory_order_relaxed);

        std::cerr << "Hit #" << i << ": Count=" << n  //
                  << " Hits=" << hits                 //
                  << " Hit Rate (%)=" << 100 * avg(hits) << std::endl;
    }

    for (std::size_t i = 0; i < min.size(); ++i)
    {
        auto& info = min[i];

        if (!(n = info[0].load(std::memory_order_relaxed)))
            continue;

        auto minValue = info[1].load(std::memory_order_relaxed);

        std::cerr << "Min #" << i << ": Count=" << n  //
                  << " Min=" << minValue << std::endl;
    }

    for (std::size_t i = 0; i < max.size(); ++i)
    {
        auto& info = max[i];

        if (!(n = info[0].load(std::memory_order_relaxed)))
            continue;

        auto maxValue = info[1].load(std::memory_order_relaxed);

        std::cerr << "Max #" << i << ": Count=" << n  //
                  << " Max=" << maxValue << std::endl;
    }

    for (std::size_t i = 0; i < extreme.size(); ++i)
    {
        auto& info = extreme[i];

        if (!(n = info[0].load(std::memory_order_relaxed)))
            continue;

        auto minValue = info[1].load(std::memory_order_relaxed);
        auto maxValue = info[2].load(std::memory_order_relaxed);

        std::cerr << "Extreme #" << i << ": Count=" << n  //
                  << " Min=" << minValue                  //
                  << " Max=" << maxValue << std::endl;
    }

    for (std::size_t i = 0; i < mean.size(); ++i)
    {
        auto& info = mean[i];

        if (!(n = info[0].load(std::memory_order_relaxed)))
            continue;

        auto sum = info[1].load(std::memory_order_relaxed);

        std::cerr << "Mean #" << i << ": Count=" << n  //
                  << " Sum=" << sum                    //
                  << " Mean=" << avg(sum) << std::endl;
    }

    for (std::size_t i = 0; i < stdev.size(); ++i)
    {
        auto& info = stdev[i];

        if (!(n = info[0].load(std::memory_order_relaxed)))
            continue;

        auto sum   = info[1].load(std::memory_order_relaxed);
        auto sumSq = info[2].load(std::memory_order_relaxed);

        auto r = std::sqrt(avg(sumSq) - sqr(avg(sum)));

        std::cerr << "Stdev #" << i << ": Count=" << n  //
                  << " Stdev=" << r << std::endl;
    }

    for (std::size_t i = 0; i < correl.size(); ++i)
    {
        auto& info = correl[i];

        if (!(n = info[0].load(std::memory_order_relaxed)))
            continue;

        auto sumV1   = info[1].load(std::memory_order_relaxed);
        auto sumSqV1 = info[2].load(std::memory_order_relaxed);
        auto sumV2   = info[3].load(std::memory_order_relaxed);
        auto sumSqV2 = info[4].load(std::memory_order_relaxed);
        auto sumV1V2 = info[5].load(std::memory_order_relaxed);

        auto r = (avg(sumV1V2) - avg(sumV1) * avg(sumV2))
               / (std::sqrt(avg(sumSqV1) - sqr(sumV1)) * std::sqrt(avg(sumSqV2) - sqr(avg(sumV2))));

        std::cerr << "Correl #" << i << ": Count=" << n  //
                  << " Coefficient=" << r << std::endl;
    }
}
}  // namespace Debug
#endif

CommandLine::CommandLine(int argc, const char* argv[]) noexcept {
    std::size_t argSize = argc;

    arguments.reserve(argSize);

    for (std::size_t i = 0; i < argSize; ++i)
        arguments.emplace_back(argv[i]);  // no copy, just view
}

// Extract the binary directory
std::string CommandLine::binary_directory(std::string_view path) noexcept {
#if defined(_WIN32)
    std::string pathSeparator = "\\";
    #if defined(_MSC_VER)
    // Under windows path may not have the extension.
    // Also _get_pgmptr() had issues in some Windows 10 versions,
    // so check returned values carefully.
    char* pgmPtr = nullptr;
    if (_get_pgmptr(&pgmPtr) == 0 && pgmPtr != nullptr && *pgmPtr)
        path = pgmPtr;  // NOT std::string{pgmPtr}
    #endif
#else
    std::string pathSeparator = "/";
#endif
    // now owns memory for resizing etc.
    std::string binaryDirectory{path};

    std::string currentDirectory{"."};
    currentDirectory += pathSeparator;

    std::size_t size = binaryDirectory.find_last_of("\\/");

    if (size == std::string::npos)
        binaryDirectory = currentDirectory;
    else
        binaryDirectory.resize(size + 1);

    // Pattern replacement: "./" at the start of path is replaced by the working directory
    if (binaryDirectory.find(currentDirectory) == 0)
        binaryDirectory.replace(0, 1, working_directory());

    return binaryDirectory;
}
// Extract the working directory
std::string CommandLine::working_directory() noexcept {
    std::string workingDirectory;

    StdArray<char, PATH_MAX> currentWorkingDirectory{};

    char* cwd = GETCWD(currentWorkingDirectory.data(), currentWorkingDirectory.size());
    if (cwd == nullptr)
        DEBUG_LOG("GETCWD(): Failed");
    else
        workingDirectory = cwd;

    return workingDirectory;
}

std::size_t str_to_size_t(std::string_view str) noexcept {
    // Use from_chars (no allocation, fast)
    const char* begin = str.data();
    const char* end   = begin + str.size();

    unsigned long long value = 0;

    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end)
        std::exit(EXIT_FAILURE);

    if (value > std::numeric_limits<std::size_t>::max())
        std::exit(EXIT_FAILURE);

    return std::size_t(value);
}

std::optional<std::string> read_file_to_string(std::string_view filePath) noexcept {

    std::ifstream ifs{std::string{filePath}, std::ios::binary | std::ios::ate};
    if (!ifs)
        return std::nullopt;

    auto size = ifs.tellg();

    if (size < 0)
        return std::nullopt;

    std::string str;
    str.reserve(std::size_t(size));

    ifs.seekg(0, std::ios::beg);

    //str.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());

    if (!ifs.read(str.data(), size))
        return std::nullopt;

    return str;
}

}  // namespace DON
