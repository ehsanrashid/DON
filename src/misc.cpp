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
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>

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
constexpr std::string_view Version{"dev"};
constexpr std::string_view Author{"Ehsan Rashid"};

#if !defined(GIT_DATE)
// Format date to YYYYMMDD
std::string format_date(std::string_view date) noexcept {
    //constexpr std::string_view Months{"Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec"};
    constexpr StdArray<std::string_view, 12> Months{
      "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"  //
    };

    constexpr std::string_view NullDate{"00000000"};

    // Tokenize: expect "Mon DD YYYY" where DD may have a trailing comma.
    // Format from compiler: "Sep 02 2008"
    std::istringstream iss{std::string(date)};

    std::string month, day, year;
    iss >> month >> day >> year;

    if (iss.fail())
        return std::string(NullDate);
    // Trim possible trailing comma from day (e.g. "21,")
    if (!day.empty() && day.back() == ',')
        day.pop_back();
    // Basic validation: month is 3 letters, day 1-2 digits, year 4 digits
    if (month.size() != 3 || day.empty() || day.size() > 2 || year.size() != 4)
        return std::string(NullDate);
    // Ensure day and year are numeric
    if (!std::all_of(day.begin(), day.end(), [](unsigned char c) { return std::isdigit(c); })
        || !std::all_of(year.begin(), year.end(), [](unsigned char c) { return std::isdigit(c); }))
        return std::string(NullDate);
    // Find month index (1..12)
    auto itr = std::find(Months.begin(), Months.end(), std::string_view(month));
    if (itr == Months.end())
        return std::string(NullDate);

    //unsigned monthIndex = 1 + Months.find(month) / 4;
    unsigned monthIndex = 1 + std::distance(Months.begin(), itr);

    // Format YYYYMMDD using ostringstream
    std::ostringstream oss;
    oss << std::setfill('0')           //
        << std::setw(4) << year        //
        << std::setw(2) << monthIndex  //
        << std::setw(2) << day;
    return oss.str();
}
#endif

}  // namespace

std::string engine_info(bool uci) noexcept {
    std::string str;
    str.reserve(64);

    if (uci)
        str += "id name ";

    str += version_info();

    if (uci)
        str += "\nid author ";
    else
        str += " by ";

    str += Author;

    return str;
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
    std::string str;
    str.reserve(32);

    str += Name;
    str += ' ';
    str += Version;

    if constexpr (Version == "dev")
    {
        str += '-';
#if defined(GIT_DATE)
        str += STRINGIFY(GIT_DATE);
#else
        str += format_date(__DATE__);
#endif
        str += '-';
#if defined(GIT_SHA)
        str += STRINGIFY(GIT_SHA);
#else
        str += "nogit";
#endif
    }

    return str;
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

    std::string str;
    str.reserve(256);

    str += "\nCompiled by                : ";
#if defined(__INTEL_LLVM_COMPILER)
    str += "ICX ";
    str += STRINGIFY(__INTEL_LLVM_COMPILER);
#elif defined(__clang__)
    str += "clang++ ";
    str += VERSION_STRING(__clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(_MSC_VER)
    str += "MSVC ";
    str += STRINGIFY(_MSC_FULL_VER) "." STRINGIFY(_MSC_BUILD);
#elif defined(__GNUC__)
    str += "g++ (GNUC) ";
    str += VERSION_STRING(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(__e2k__) && defined(__LCC__)
    str += "MCST LCC ";
    str += std::to_string(__LCC__ / 100);
    str += ".";
    str += std::to_string(__LCC__ % 100);
    str += ".";
    str += std::to_string(__LCC_MINOR__);
#else
    str += "(unknown compiler)";
#endif

    str += "\nCompiled on                : ";
#if defined(__APPLE__)
    str += "Apple";
#elif defined(__CYGWIN__)
    str += "Cygwin";
#elif defined(__MINGW64__)
    str += "MinGW64";
#elif defined(__MINGW32__)
    str += "MinGW32";
#elif defined(__ANDROID__)
    str += "Android";
#elif defined(__linux__)
    str += "Linux";
#elif defined(_WIN64)
    str += "Microsoft Windows 64-bit";
#elif defined(_WIN32)
    str += "Microsoft Windows 32-bit";
#else
    str += "(unknown system)";
#endif

    str += "\nCompilation architecture   : ";
#if defined(ARCH)
    str += STRINGIFY(ARCH);
#else
    str += "(undefined architecture)";
#endif

    str += "\nCompilation settings       : ";
#if defined(IS_64BIT)
    str += "64-bit";
#else
    str += "32-bit";
#endif
#if defined(USE_AVX512ICL)
    str += " AVX512ICL";
#endif
#if defined(USE_VNNI)
    str += " VNNI";
#endif
#if defined(USE_AVX512)
    str += " AVX512";
#endif
#if defined(USE_BMI2)
    str += " BMI2";
    #if defined(USE_COMPRESSED)
    str += "-(Compressed)";
    #endif
#endif
#if defined(USE_AVX2)
    str += " AVX2";
#endif
#if defined(USE_SSE41)
    str += " SSE41";
#endif
#if defined(USE_SSSE3)
    str += " SSSE3";
#endif
#if defined(USE_SSE2)
    str += " SSE2";
#endif
#if defined(USE_NEON_DOTPROD)
    str += " NEON_DOTPROD";
#elif defined(USE_NEON)
    str += " NEON";
#endif
#if defined(USE_POPCNT)
    str += " POPCNT";
#endif

#if !defined(NDEBUG)
    str += " DEBUG";
#endif

    str += "\nCompiler __VERSION__ macro : ";
#if defined(__VERSION__)
    str += __VERSION__;
#else
    str += "(undefined macro)";
#endif

#undef VERSION_STRING

    return str;
}

std::string format_time(const std::chrono::system_clock::time_point& timePoint) noexcept {
    std::time_t  time = std::chrono::system_clock::to_time_t(timePoint);
    std::int64_t usec =
      std::chrono::duration_cast<std::chrono::microseconds>(timePoint.time_since_epoch()).count()
      % 1000000;

    std::tm tm{};
#if defined(_WIN32)  // Windows
    localtime_s(&tm, &time);
#elif defined(__unix__) || defined(__APPLE__)  // POSIX (Linux / macOS)
    localtime_r(&time, &tm);
#else
    // Fallback (not thread-safe)
    tm = *std::localtime(&time);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y.%m.%d-%H:%M:%S") << '.'  //
        << std::setfill('0') << std::setw(6) << usec;
    return oss.str();
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
             && !mn.compare_exchange_weak(minValue, value, std::memory_order_relaxed,
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
             && !mx.compare_exchange_weak(maxValue, value, std::memory_order_relaxed,
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
             && !mn.compare_exchange_weak(minValue, value, std::memory_order_relaxed,
                                          std::memory_order_relaxed);)
        {}
    }
    {
        auto& mx = info[2];
        for (auto maxValue = mx.load(std::memory_order_relaxed);
             maxValue < value
             && !mx.compare_exchange_weak(maxValue, value, std::memory_order_relaxed,
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
    const auto   avg = [&n](std::int64_t x) noexcept { return double(x) / n; };

    for (std::size_t i = 0; i < hit.size(); ++i)
    {
        const auto& info = hit[i];

        if (!(n = info[0].load(std::memory_order_relaxed)))
            continue;

        auto hits = info[1].load(std::memory_order_relaxed);

        std::cerr << "Hit #" << i << ": Count=" << n  //
                  << " Hits=" << hits                 //
                  << " Hit Rate (%)=" << 100 * avg(hits) << std::endl;
    }

    for (std::size_t i = 0; i < min.size(); ++i)
    {
        const auto& info = min[i];

        if (!(n = info[0].load(std::memory_order_relaxed)))
            continue;

        auto minValue = info[1].load(std::memory_order_relaxed);

        std::cerr << "Min #" << i << ": Count=" << n  //
                  << " Min=" << minValue << std::endl;
    }

    for (std::size_t i = 0; i < max.size(); ++i)
    {
        const auto& info = max[i];

        if (!(n = info[0].load(std::memory_order_relaxed)))
            continue;

        auto maxValue = info[1].load(std::memory_order_relaxed);

        std::cerr << "Max #" << i << ": Count=" << n  //
                  << " Max=" << maxValue << std::endl;
    }

    for (std::size_t i = 0; i < extreme.size(); ++i)
    {
        const auto& info = extreme[i];

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
        const auto& info = mean[i];

        if (!(n = info[0].load(std::memory_order_relaxed)))
            continue;

        auto sum = info[1].load(std::memory_order_relaxed);

        std::cerr << "Mean #" << i << ": Count=" << n  //
                  << " Sum=" << sum                    //
                  << " Mean=" << avg(sum) << std::endl;
    }

    for (std::size_t i = 0; i < stdev.size(); ++i)
    {
        const auto& info = stdev[i];

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
        const auto& info = correl[i];

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
    arguments.reserve(argc);
    for (int i = 0; i < argc; ++i)
        arguments.emplace_back(argv[i]);  // no copy, just view
}

// Extract the binary directory path
std::string CommandLine::binary_directory(std::string path) noexcept {
    std::string pathSeparator;

#if defined(_WIN32)
    pathSeparator = "\\";
    #if defined(_MSC_VER)
    // Under windows path may not have the extension.
    // Also _get_pgmptr() had issues in some Windows 10 versions,
    // so check returned values carefully.
    char* pgmptr = nullptr;
    if (_get_pgmptr(&pgmptr) == 0 && pgmptr != nullptr && *pgmptr)
        path = pgmptr;
    #endif
#else
    pathSeparator = "/";
#endif

    std::string binaryDirectory = path;

    std::size_t pos = binaryDirectory.find_last_of("\\/");
    if (pos == std::string::npos)
        binaryDirectory = "." + pathSeparator;
    else
        binaryDirectory.resize(pos + 1);

    // Pattern replacement: "./" at the start of path is replaced by the working directory
    if (binaryDirectory.find("." + pathSeparator) == 0)
        binaryDirectory.replace(0, 1, CommandLine::working_directory());

    return binaryDirectory;
}
// Extract the working directory
std::string CommandLine::working_directory() noexcept {

    StdArray<char, 4096> buffer{};

    char* cwd = GETCWD(buffer.data(), buffer.size());

    std::string workingDirectory;
    if (cwd != nullptr)
        workingDirectory = cwd;

    return workingDirectory;
}

std::size_t str_to_size_t(std::string_view str) noexcept {

    unsigned long long value = std::stoull(std::string(str));
    if (value > std::numeric_limits<std::size_t>::max())
        std::exit(EXIT_FAILURE);
    return static_cast<std::size_t>(value);
}

std::optional<std::string> read_file_to_string(std::string_view filePath) noexcept {

    std::ifstream ifs(std::string(filePath), std::ios::binary);
    if (!ifs)
        return std::nullopt;

    return std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
}

}  // namespace DON
