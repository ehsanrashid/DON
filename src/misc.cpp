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
#include <ctime>
#include <limits>

#if defined(_WIN32)
    #if !defined(NOMINMAX)
        #define NOMINMAX
    #endif
    #include <shellapi.h>
#endif

namespace DON {

namespace {

constexpr std::string_view Name{"DON"};
constexpr std::string_view Author{"Ehsan Rashid"};
constexpr std::string_view Version{"dev"};

// Format date to YYYYMMDD
[[maybe_unused]] std::string format_date(std::string_view date) noexcept {
    constexpr std::string_view NullDate{"00000000"};

    // Tokenize: expect "Mon DD YYYY" where DD may have a trailing comma.
    // Format from compiler: "Sep 02 2008"

    if (date.size() < 8)
        return std::string{NullDate};

    // Parse month (first 3 chars), then skip space(s), then day, then space, then year
    const auto*       p   = date.data();
    const auto* const end = p + date.size();

    // Parse month (first 3 chars)
    if (end - p < 3)
        return std::string{NullDate};

    std::string_view m{p, 3};
    p += 3;

    // Find month index (1..12)
    unsigned month = to_month(m);
    if (month == 0)
        return std::string{NullDate};

    // Skip spaces
    for (; p < end && std::isspace(uchar(*p)); ++p)
    {}

    // Parse day (1-2 digits)
    if (end - p < 1 || !std::isdigit(uchar(*p)))
        return std::string{NullDate};

    unsigned day = 0;
    for (; p < end && std::isdigit(uchar(*p)); ++p)
    {
        day = 10 * day + char_to_digit(*p);
    }

    // Validate day range
    if (day < 1 || day > 31)
        return std::string{NullDate};

    // Skip spaces/comma
    for (; p < end && (std::isspace(uchar(*p)) || *p == ','); ++p)
    {}

    // Parse year (4 digits)
    if (end - p < 4)
        return std::string{NullDate};

    unsigned year = 0;
    for (const auto* yEnd = p + 4; p != yEnd; ++p)
    {
        if (!std::isdigit(uchar(*p)))
            return std::string{NullDate};

        year = 10 * year + char_to_digit(*p);
    }

    // Validate year range (reasonable bounds)
    if (year < 1970)
        return std::string{NullDate};

    // Format YYYYMMDD manually (faster than snprintf)
    Array<char, 8> buffer  // 8 chars
      {
        digit_to_char(year / 1000 % 10),  //
        digit_to_char(year / 100 % 10),   //
        digit_to_char(year / 10 % 10),    //
        digit_to_char(year % 10),         //
        digit_to_char(month / 10 % 10),   //
        digit_to_char(month % 10),        //
        digit_to_char(day / 10 % 10),     //
        digit_to_char(day % 10)           //
      };

    return std::string{buffer.data(), buffer.size()};
}

// Format time HH:MM:SS -> HHMMSS
[[maybe_unused]] std::string format_time(std::string_view time) noexcept {

    constexpr std::string_view NullTime{"000000"};

    // Expect exactly "HH:MM:SS"
    if (time.size() != 8)
        return std::string{NullTime};

    const auto* p = time.data();

    // Validate structure
    if (!std::isdigit(uchar(p[0])) || !std::isdigit(uchar(p[1])) || p[2] != ':'
        || !std::isdigit(uchar(p[3])) || !std::isdigit(uchar(p[4])) || p[5] != ':'
        || !std::isdigit(uchar(p[6])) || !std::isdigit(uchar(p[7])))
        return std::string{NullTime};

    unsigned hour = 10 * char_to_digit(p[0]) + char_to_digit(p[1]);

    unsigned min = 10 * char_to_digit(p[3]) + char_to_digit(p[4]);

    unsigned sec = 10 * char_to_digit(p[6]) + char_to_digit(p[7]);

    // Range validation (important)
    if (hour > 23 || min > 59 || sec > 59)
        return std::string{NullTime};

    Array<char, 6> buffer{p[0], p[1], p[3], p[4], p[6], p[7]};

    return std::string{buffer.data(), buffer.size()};
}

}  // namespace

void set_console_input(ConsoleMode consoleMode) noexcept {
    switch (consoleMode)
    {
    case ConsoleMode::UTF7 :
#if defined(_WIN32)
        SetConsoleCP(CP_UTF7);
#else
      ;
#endif
        break;
    case ConsoleMode::EnableVirtualTerminal :
        break;
    case ConsoleMode::FullyFeatured :
        break;
    case ConsoleMode::Default :
        break;
    case ConsoleMode::UTF8 :
    default :
#if defined(_WIN32)
        SetConsoleCP(CP_UTF8);
#else
      ;
#endif
    }
}
void set_console_output(ConsoleMode consoleMode) noexcept {
    switch (consoleMode)
    {
    case ConsoleMode::UTF7 :
#if defined(_WIN32)
        SetConsoleOutputCP(CP_UTF7);
#else
      ;
#endif
        break;
    case ConsoleMode::EnableVirtualTerminal :
        break;
    case ConsoleMode::FullyFeatured :
        break;
    case ConsoleMode::Default :
        break;
    case ConsoleMode::UTF8 :
    default :
#if defined(_WIN32)
        SetConsoleOutputCP(CP_UTF8);
#else
      ;
#endif
    }
}

std::string engine_info(bool uci) noexcept {
    std::string engine;
    engine.reserve(64);

    engine.assign(uci ? "id name " : "")
      .append(version_info())
      .append(uci ? "\nid author " : " by ")
      .append(Author);

    return engine;
}

void show_logo() noexcept {
    auto border = [](std::string_view sv) {
        std::cout << ConsoleColor::BG_BLACK                                    //
                  << ConsoleColor::BRIGHT_YELLOW << ConsoleColor::BLINK << sv  //
                  << ConsoleColor::RESET << '\n';
    };
    auto mid1 = [](std::string_view sv, const char* color1) {
        std::cout                                                         //
          << ConsoleColor::BG_BLACK                                       //
          << ConsoleColor::BRIGHT_YELLOW << ConsoleColor::BLINK << "  ║"  //
          << ConsoleColor::RESET                                          //
          << ConsoleColor::BG_BLACK                                       //
          << color1 << sv << ConsoleColor::RESET                          //
          << ConsoleColor::BG_BLACK                                       //
          << ConsoleColor::BRIGHT_YELLOW << ConsoleColor::BLINK << "║  "  //
          << ConsoleColor::RESET << '\n';
    };
    auto mid2 = [](std::string_view sv, const char* color1, const char* color2) {
        std::cout                                                         //
          << ConsoleColor::BG_BLACK                                       //
          << ConsoleColor::BRIGHT_YELLOW << ConsoleColor::BLINK << "  ║"  //
          << ConsoleColor::RESET                                          //
          << ConsoleColor::BG_BLACK                                       //
          << color1 << color2 << sv << ConsoleColor::RESET                //
          << ConsoleColor::BG_BLACK                                       //
          << ConsoleColor::BRIGHT_YELLOW << ConsoleColor::BLINK << "║  "  //
          << ConsoleColor::RESET << '\n';
    };
    std::cout << '\n';
    // clang-format off
    border("  ╔══════════════════════════════╗  ");
         mid1("  ██████╗ ╔██████╗ ███╗  ██╗  ", ConsoleColor::RED);
         mid2("  ██╔══██╗██╔═══██╗████╗ ██║  ", ConsoleColor::BRIGHT_RED, ConsoleColor::STRIKETHROUGH);
         mid2("  ██║  ██║██║   ██║██╔██╗██║  ", ConsoleColor::BRIGHT_RED, ConsoleColor::STRIKETHROUGH);
         mid2("  ██║  ██║██║   ██║██║╚████║  ", ConsoleColor::BRIGHT_RED, ConsoleColor::STRIKETHROUGH);
         mid1("  ██████╔╝╚██████╔╝██║ ╚███║  ", ConsoleColor::RED);
         mid1("  ╚═════╝  ╚═════╝ ╚═╝  ╚══╝  ", ConsoleColor::RED);
    border("  ╚══════════════════════════════╝  ");
    // clang-format on
    std::cout << '\n';
}

// Returns the full human-readable DON version string.
//
// Development builds:
//   • If Git metadata is available, append commit information:
//       DON dev-YYYYMMDD-SHA
//
//   • If Git metadata is unavailable (e.g. local/source builds),
//     fall back to a timestamp-based identifier:
//       DON dev-YYYYMMDD-HHMMSS-nogit
//
// Release builds:
//   • Only include the semantic version number:
//       DON X.Y (version)
std::string version_info() noexcept {
    std::string version;
    version.reserve(32);

    version.assign(Name).append(" ").append(Version);

    if constexpr (Version == "dev")
    {
        version.push_back('-');
#if defined(GIT_DATE)
        version.append(STRINGIFY(GIT_DATE));
#else
        version.append(format_date(__DATE__));
#endif
        version.push_back('-');
#if defined(GIT_SHA)
        version.append(STRINGIFY(GIT_SHA));
#else
        version.append(format_time(__TIME__)).append("-nogit");
#endif
#if defined(GIT_DIFFINDEX)
        version.append("-m");
        version.append(STRINGIFY(GIT_DIFFINDEX));
#endif
    }

    return version;
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

    std::string compiler;
    compiler.reserve(256);

    compiler.assign("\nCompiled by                : ");
#if defined(__INTEL_LLVM_COMPILER)
    compiler  //
      .append("ICX ")
      .append(STRINGIFY(__INTEL_LLVM_COMPILER));
#elif defined(__clang__)
    compiler  //
      .append("clang++ ")
      .append(VERSION_STRING(__clang_major__, __clang_minor__, __clang_patchlevel__));
#elif defined(__GNUC__)
    compiler  //
      .append("g++ (GNUC) ")
      .append(VERSION_STRING(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__));
#elif defined(_MSC_VER)
    compiler  //
      .append("MSVC ")
      .append(std::to_string(_MSC_VER / 100))  // major
      .append(1, '.')
      .append(std::to_string(_MSC_VER % 100))  // minor
      .append(1, '.')
      .append(std::to_string(_MSC_FULL_VER % 100000))  // patch
    #if defined(_MSC_BUILD)
      .append(1, '.')
      .append(std::to_string(_MSC_BUILD))  // build
    #endif
      ;
#elif defined(__e2k__) && defined(__LCC__)
    compiler  //
      .append("MCST LCC ")
      .append(std::to_string(__LCC__ / 100))  // major
      .append(1, '.')
      .append(std::to_string(__LCC__ % 100))  // minor
    #if defined(__LCC_MINOR__)
      .append(1, '.')
      .append(std::to_string(__LCC_MINOR__))  // patch
    #endif
      ;
#else
    compiler.append("(unknown compiler)");
#endif

    compiler.append("\nCompiled on                : ");
#if defined(__APPLE__)
    compiler.append("Apple");
#elif defined(__CYGWIN__)
    compiler.append("Cygwin");
#elif defined(__MINGW64__)
    compiler.append("MinGW64");
#elif defined(__MINGW32__)
    compiler.append("MinGW32");
#elif defined(__ANDROID__)
    compiler.append("Android");
#elif defined(__linux__)
    compiler.append("Linux");
#elif defined(_WIN32)
    #if defined(_WIN64)
    compiler.append("Microsoft Windows 64-bit");
    #else
    compiler.append("Microsoft Windows 32-bit");
    #endif
#else
    compiler.append("(unknown system)");
#endif

    compiler.append("\nCompilation architecture   : ");
#if defined(ARCH)
    compiler.append(STRINGIFY(ARCH));
#else
    compiler.append("(unknown architecture)");
#endif

    compiler.append("\nCompilation settings       : ");
#if defined(IS_64BIT)
    compiler.append("64-bit");
#else
    compiler.append("32-bit");
#endif
#if defined(USE_AVX512ICL)
    compiler.append(" AVX512ICL");
#endif
#if defined(USE_VNNI)
    compiler.append(" VNNI");
#endif
#if defined(USE_AVX512)
    compiler.append(" AVX512");
#endif
#if defined(USE_BMI2)
    compiler.append(" BMI2");
    #if defined(USE_CMP)
    compiler.append("-CMP");
    #endif
#endif
#if defined(USE_AVX2)
    compiler.append(" AVX2");
#endif
#if defined(USE_SSE41)
    compiler.append(" SSE41");
#endif
#if defined(USE_SSSE3)
    compiler.append(" SSSE3");
#endif
#if defined(USE_SSE2)
    compiler.append(" SSE2");
#endif
#if defined(USE_NEON)
    #if defined(USE_NEON_DOTPROD)
    compiler.append(" NEON_DOTPROD");
    #else
    compiler.append(" NEON");
    #endif
#endif
#if defined(USE_LASX)
    compiler.append(" LASX");
#endif
#if defined(USE_LSX)
    compiler.append(" LSX");
#endif
#if defined(USE_POPCNT)
    compiler.append(" POPCNT");
#endif

#if !defined(NDEBUG)
    compiler.append(" DEBUG");
#endif

    compiler.append("\nCompiler __VERSION__ macro : ");
#if defined(__VERSION__)
    compiler.append(__VERSION__);
#else
    compiler.append("(unknown macro)");
#endif

#undef VERSION_STRING

    return compiler;
}

std::string format_time(const std::chrono::system_clock::time_point& timePoint) noexcept {
    // clang-format off
    std::time_t time = std::chrono::system_clock::to_time_t(timePoint);
    u64 usec         = std::chrono::duration_cast<std::chrono::microseconds>(timePoint.time_since_epoch()).count() % 1000000;

    std::tm tm{};
#if defined(_WIN32)  // Windows
    localtime_s(&tm, &time);
#elif defined(__unix__) || defined(__APPLE__)  // POSIX (Linux / macOS)
    localtime_r(&time, &tm);
#else
    // Fallback (not thread-safe)
    tm = *std::localtime(&time);
#endif

    Array<char, 32> buffer{};

    usize writtenSize = 0;
    // Format the YYYY.MM.DD-HH:MM:SS part
    writtenSize += std::strftime(buffer.data(), buffer.size(), "%Y.%m.%d-%H:%M:%S", &tm);
    // Append microseconds safely
    writtenSize += std::snprintf(buffer.data() + writtenSize, buffer.size() - writtenSize, ".%06" PRIu64, usec);
    // clang-format on
    return std::string{buffer.data(), std::min(writtenSize, buffer.size() - 1)};
}

std::ostream& operator<<(std::ostream& os, const FixedText& fixedText) noexcept {

    os.write(fixedText.c_str(), std::streamsize(fixedText.size()));

    return os;
}

#if !defined(NDEBUG)
// Debug functions used mainly to collect run-time statistics
namespace Debug {
namespace {

template<usize Size>
class Info {
   public:
    Info() noexcept {
        for (usize i = 0; i < Size; ++i)
            data[i].store(0, std::memory_order_relaxed);
    }

    Info(const Info& info) noexcept {
        for (usize i = 0; i < Size; ++i)
            data[i].store(info.data[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    Info& operator=(const Info& info) noexcept {
        if (this == &info)
            return *this;

        for (usize i = 0; i < Size; ++i)
            data[i].store(info.data[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
        return *this;
    }

    Info(Info&&) noexcept            = delete;
    Info& operator=(Info&&) noexcept = delete;

    [[nodiscard]] decltype(auto) operator[](usize index) const noexcept {
        assert(index < Size && "Index out of bounds");
        return data[index];
    }
    [[nodiscard]] decltype(auto) operator[](usize index) noexcept {
        assert(index < Size && "Index out of bounds");
        return data[index];
    }

   protected:
    Array<std::atomic<i64>, Size> data;
};

class MinInfo final: public Info<2> {
   public:
    MinInfo() noexcept {
        data[1].store(std::numeric_limits<i64>::max(), std::memory_order_relaxed);
    }
};

class MaxInfo final: public Info<2> {
   public:
    MaxInfo() noexcept {
        data[1].store(std::numeric_limits<i64>::min(), std::memory_order_relaxed);
    }
};

class ExtremeInfo final: public Info<3> {
   public:
    ExtremeInfo() noexcept {
        data[1].store(std::numeric_limits<i64>::max(), std::memory_order_relaxed);
        data[2].store(std::numeric_limits<i64>::min(), std::memory_order_relaxed);
    }
};


constexpr usize SLOT_MAX = 64;

Array<Info<2>, SLOT_MAX>     hit;
Array<MinInfo, SLOT_MAX>     min;
Array<MaxInfo, SLOT_MAX>     max;
Array<ExtremeInfo, SLOT_MAX> extreme;
Array<Info<2>, SLOT_MAX>     mean;
Array<Info<3>, SLOT_MAX>     stdev;
Array<Info<6>, SLOT_MAX>     correl;

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

void hit_on(bool cond, usize slot) noexcept {
    assert(slot < hit.size());
    if (slot >= hit.size())
        return;
    auto& info = hit[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    if (cond)
        info[1].fetch_add(1, std::memory_order_relaxed);
}

void min_of(i64 value, usize slot) noexcept {
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

void max_of(i64 value, usize slot) noexcept {
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

void extreme_of(i64 value, usize slot) noexcept {
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

void mean_of(i64 value, usize slot) noexcept {
    assert(slot < mean.size());
    if (slot >= mean.size())
        return;
    auto& info = mean[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    info[1].fetch_add(value, std::memory_order_relaxed);
}

void stdev_of(i64 value, usize slot) noexcept {
    assert(slot < stdev.size());
    if (slot >= stdev.size())
        return;
    auto& info = stdev[slot];

    info[0].fetch_add(1, std::memory_order_relaxed);
    info[1].fetch_add(value, std::memory_order_relaxed);
    info[2].fetch_add(value * value, std::memory_order_relaxed);
}

void correl_of(i64 value1, i64 value2, usize slot) noexcept {
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

    i64  n;
    auto avg = [&n](i64 x) noexcept { return double(x) / n; };

    for (usize i = 0; i < hit.size(); ++i)
    {
        auto& info = hit[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        auto hits = info[1].load(std::memory_order_relaxed);

        std::cerr << "Hit #" << i << ": Count=" << n  //
                  << " Hits=" << hits                 //
                  << " Hit Rate (%)=" << 100 * avg(hits) << std::endl;
    }

    for (usize i = 0; i < min.size(); ++i)
    {
        auto& info = min[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        auto minValue = info[1].load(std::memory_order_relaxed);

        std::cerr << "Min #" << i << ": Count=" << n  //
                  << " Min=" << minValue << std::endl;
    }

    for (usize i = 0; i < max.size(); ++i)
    {
        auto& info = max[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        auto maxValue = info[1].load(std::memory_order_relaxed);

        std::cerr << "Max #" << i << ": Count=" << n  //
                  << " Max=" << maxValue << std::endl;
    }

    for (usize i = 0; i < extreme.size(); ++i)
    {
        auto& info = extreme[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        auto minValue = info[1].load(std::memory_order_relaxed);
        auto maxValue = info[2].load(std::memory_order_relaxed);

        std::cerr << "Extreme #" << i << ": Count=" << n  //
                  << " Min=" << minValue                  //
                  << " Max=" << maxValue << std::endl;
    }

    for (usize i = 0; i < mean.size(); ++i)
    {
        auto& info = mean[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        auto sum = info[1].load(std::memory_order_relaxed);

        std::cerr << "Mean #" << i << ": Count=" << n  //
                  << " Sum=" << sum                    //
                  << " Mean=" << avg(sum) << std::endl;
    }

    for (usize i = 0; i < stdev.size(); ++i)
    {
        auto& info = stdev[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
            continue;

        auto sum   = info[1].load(std::memory_order_relaxed);
        auto sumSq = info[2].load(std::memory_order_relaxed);

        auto r = std::sqrt(avg(sumSq) - sqr(avg(sum)));

        std::cerr << "Stdev #" << i << ": Count=" << n  //
                  << " Stdev=" << r << std::endl;
    }

    for (usize i = 0; i < correl.size(); ++i)
    {
        auto& info = correl[i];

        if ((n = info[0].load(std::memory_order_relaxed)) == 0)
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
#if defined(_WIN32)
    int     wargc = 0;
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);

    if (wargv != nullptr)
    {
        argStorage.reserve(usize(wargc));

        for (int i = 0; i < wargc; ++i)
            argStorage.emplace_back(utf8_from_wstring(wargv[i]));

        LocalFree(wargv);

        arguments.reserve(argStorage.size());

        for (const auto& arg : argStorage)
            arguments.emplace_back(arg);
    }
    else
    {
        set_arguments(argc, argv);
    }
#else
    set_arguments(argc, argv);
#endif
}

void CommandLine::set_arguments(int argc, const char* argv[]) noexcept {
    usize argCount = argc;

    arguments.reserve(argCount);

    for (usize i = 0; i < argCount; ++i)
        arguments.emplace_back(argv[i]);  // no copy, just view
}

std::filesystem::path CommandLine::binary_directory(std::filesystem::path path) noexcept {
#if defined(_WIN32)
    // Prefer the executable path reported by Windows.
    // Unlike _get_wpgmptr, this does not depend on whether the CRT used a narrow or wide entry point.
    // Windows paths cannot exceed 32767 characters, so a fixed buffer is always sufficient.
    // Falls back to path if the API fails.
    constexpr DWORD BuffSize = 32768;
    WCHAR           filename[BuffSize]{};

    DWORD length = GetModuleFileNameW(nullptr, filename, BuffSize);
    if (length != 0 && length < BuffSize)
        path = std::filesystem::path(filename, filename + length);
#endif

    auto binaryDirectory{path.parent_path()};
    return binaryDirectory.empty() ? std::filesystem::path(".") : binaryDirectory;
}
std::filesystem::path CommandLine::working_directory() noexcept {
    return std::filesystem::current_path();
}

std::string utf8_from_wstring(std::wstring_view wSv) noexcept {
#if defined(_WIN32)
    if (wSv.empty())
        return {};

    const int size =
      WideCharToMultiByte(CP_UTF8, 0, wSv.data(), int(wSv.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};

    std::string str(usize(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wSv.data(), int(wSv.size()), str.data(), size, nullptr,
                        nullptr);
    return str;
#else
    return std::string{wSv.begin(), wSv.end()};
#endif
}

std::filesystem::path path_from_utf8(std::string_view path) noexcept {
#if defined(_WIN32)
    const usize size = path.size();
    if (size > INT_MAX)
        return {};
    int u8Size = int(size);
    int wSize  = MultiByteToWideChar(CP_UTF8, 0, path.data(), u8Size, nullptr, 0);

    std::wstring wStr(usize(wSize), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), u8Size, wStr.data(), wSize);
    return {wStr};
#else
    return {path};
#endif
}

std::optional<usize> str_to_size_t(std::string_view sv) noexcept {
    if (sv.empty() || sv[0] == '-')
        return std::nullopt;
    // Use from_chars (no allocation, fast)
    const char* begin = sv.data();
    const char* end   = begin + sv.size();

    unsigned long long value = 0;

    auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end)
        return std::nullopt;

    if (value > std::numeric_limits<usize>::max())
        return std::nullopt;

    return usize(value);
}

std::optional<std::string> read_file_to_string(const std::filesystem::path& filePath) noexcept {

    std::ifstream ifs{filePath, std::ios::binary | std::ios::ate};
    if (!ifs)
        return std::nullopt;

    auto size = ifs.tellg();

    if (size < 0)
        return std::nullopt;

    std::string str;
    str.reserve(usize(size));

    ifs.seekg(0, std::ios::beg);

    //str.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());

    if (!ifs.read(str.data(), size))
        return std::nullopt;

    return str;
}

}  // namespace DON
