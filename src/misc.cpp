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

#include "misc.h"

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string_view>

#include "types.h"

namespace DON {

namespace {

// Version number or dev.
constexpr inline std::string_view Version{"dev"};

#if !defined(GIT_DATE)
inline std::string format_date(const std::string& date) noexcept {
    //constexpr std::string_view Months{"Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec"};
    constexpr std::array<std::string_view, 12> Months{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    std::istringstream iss(date);  // From compiler, format is "Sep 21 2008"

    std::string month, day, year;
    iss >> month >> day >> year;

    // clang-format off
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << year
        //<< std::setw(2) << (1 + Months.find(month) / 4)
        << std::setw(2) << (1 + std::distance(Months.begin(), std::find(Months.begin(), Months.end(), month)))
        << std::setw(2) << day;
    // clang-format on
    return oss.str();
}
#endif

// Fancy logging facility. The trick here is to replace cin.rdbuf() and cout.rdbuf()
// with two Tie objects that tie std::cin and std::cout to a file stream.
// Can toggle the logging of std::cout and std:cin at runtime whilst preserving
// usual I/O functionality, all without changing a single line of code!
// Idea from http://groups.google.com/group/comp.lang.c++/msg/1d941c0f26ea0d81
// MSVC requires split streambuf for std::cin and std::cout.
class Tie final: public std::streambuf {
   public:
    Tie() noexcept = delete;
    Tie(std::streambuf* b1, std::streambuf* b2) noexcept :
        buf1(b1),
        buf2(b2) {}

    int sync() noexcept override { return buf2->pubsync(), buf1->pubsync(); }
    int overflow(int ch) noexcept override { return log(buf1->sputc(char(ch)), "<< "); }
    int underflow() noexcept override { return buf1->sgetc(); }
    int uflow() noexcept override { return log(buf1->sbumpc(), ">> "); }

    int log(int ch, const char* prefix) noexcept {
        static int lastCh = '\n';  // Single log file

        if (lastCh == '\n')
            buf2->sputn(prefix, std::strlen(prefix));

        return lastCh = buf2->sputc(char(ch));
    }

    std::streambuf *buf1, *buf2;
};

class Logger final {
   private:
    Logger() noexcept = delete;
    Logger(std::istream& is, std::ostream& os) noexcept :
        istream(is),
        ostream(os),
        ofstream(),
        iTie(is.rdbuf(), ofstream.rdbuf()),
        oTie(os.rdbuf(), ofstream.rdbuf()) {}

    ~Logger() noexcept { start(""); }

    std::istream& istream;
    std::ostream& ostream;
    std::ofstream ofstream;
    Tie           iTie;
    Tie           oTie;

   public:
    static void start(const std::string& logFile) noexcept {
        static Logger logger(std::cin, std::cout);  // Tie std::cin and std::cout to a file.

        if (logger.ofstream.is_open())
        {
            logger.istream.rdbuf(logger.iTie.buf1);
            logger.ostream.rdbuf(logger.oTie.buf1);

            logger.ofstream << "[" << format_time(SystemClock::now()) << "] <-\n";
            logger.ofstream.close();
        }

        if (is_empty(logFile))
            return;

        logger.ofstream.open(logFile, std::ios_base::out | std::ios_base::app);

        if (!logger.ofstream.is_open())
        {
            std::cerr << "Unable to open debug log file: " << logFile << '\n';
            std::exit(EXIT_FAILURE);
        }
        logger.ofstream << "[" << format_time(SystemClock::now()) << "] ->\n";

        logger.istream.rdbuf(&logger.iTie);
        logger.ostream.rdbuf(&logger.oTie);
    }
};

}  // namespace

std::string engine_info(bool uci) noexcept {
    std::ostringstream oss;
    oss << (uci ? "id name " : "");
    oss << version_info();
    oss << (uci ? "\nid author " : " by ") << "Ehsan Rashid";
    return oss.str();
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
    std::ostringstream oss;
    oss << "DON" << " " << Version;

    if constexpr (Version == "dev")
    {
        oss << "-";
#if defined(GIT_DATE)
        oss << STRINGIFY(GIT_DATE);
#else
        oss << format_date(__DATE__);
#endif
        oss << "-";

#if defined(GIT_SHA)
        oss << STRINGIFY(GIT_SHA);
#else
        oss << "nogit";
#endif
    }
    return oss.str();
}

// Returns a string trying to describe the compiler used
std::string compiler_info() noexcept {

#define MAKE_VERSION_STRING(major, minor, patch) \
    STRINGIFY(major) "." STRINGIFY(minor) "." STRINGIFY(patch)

    // Predefined macros hell:
    //
    // __GNUC__                Compiler is GCC, Clang or ICX
    // __clang__               Compiler is Clang or ICX
    // __INTEL_LLVM_COMPILER   Compiler is ICX
    // _MSC_VER                Compiler is MSVC
    // _WIN32                  Building on Windows (any)
    // _WIN64                  Building on Windows 64 bit

    std::string compiler = "\nCompiled by                : ";

#if defined(__INTEL_LLVM_COMPILER)
    compiler += "ICX ";
    compiler += STRINGIFY(__INTEL_LLVM_COMPILER);
#elif defined(__clang__)
    compiler += "clang++ ";
    compiler += MAKE_VERSION_STRING(__clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(_MSC_VER)
    compiler += "MSVC ";
    compiler += "(version ";
    compiler += STRINGIFY(_MSC_FULL_VER) "." STRINGIFY(_MSC_BUILD);
    compiler += ")";
#elif defined(__e2k__) && defined(__LCC__)
    #define DOT_VER(n) \
        compiler += char('.'); \
        compiler += char('0' + (n) / 10); \
        compiler += char('0' + (n) % 10);

    compiler += "MCST LCC ";
    compiler += "(version ";
    compiler += std::to_string(__LCC__ / 100);
    DOT_VER(__LCC__ % 100) DOT_VER(__LCC_MINOR__) compiler += ")";
#elif __GNUC__
    compiler += "g++ (GNUC) ";
    compiler += MAKE_VERSION_STRING(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    compiler += "Unknown compiler ";
    compiler += "(unknown version)";
#endif

#if defined(__APPLE__)
    compiler += " on Apple";
#elif defined(__CYGWIN__)
    compiler += " on Cygwin";
#elif defined(__MINGW64__)
    compiler += " on MinGW64";
#elif defined(__MINGW32__)
    compiler += " on MinGW32";
#elif defined(__ANDROID__)
    compiler += " on Android";
#elif defined(__linux__)
    compiler += " on Linux";
#elif defined(_WIN64)
    compiler += " on Microsoft Windows 64-bit";
#elif defined(_WIN32)
    compiler += " on Microsoft Windows 32-bit";
#else
    compiler += " on unknown system";
#endif

    compiler += "\nCompilation architecture   : ";
#if defined(ARCH)
    compiler += STRINGIFY(ARCH);
#else
    compiler += "(undefined architecture)";
#endif

    compiler += "\nCompilation settings       : ";
#if defined(IS_64BIT)
    compiler += "64bit";
#else
    compiler += "32bit";
#endif
#if defined(USE_VNNI)
    compiler += " VNNI";
#endif
#if defined(USE_AVX512)
    compiler += " AVX512";
#endif
#if defined(USE_PEXT)
    compiler += " BMI2";
#endif
#if defined(USE_AVX2)
    compiler += " AVX2";
#endif
#if defined(USE_SSE41)
    compiler += " SSE41";
#endif
#if defined(USE_SSSE3)
    compiler += " SSSE3";
#endif
#if defined(USE_SSE2)
    compiler += " SSE2";
#endif
#if defined(USE_POPCNT)
    compiler += " POPCNT";
#endif
#if defined(USE_NEON_DOTPROD)
    compiler += " NEON_DOTPROD";
#elif defined(USE_NEON)
    compiler += " NEON";
#endif

#if !defined(NDEBUG)
    compiler += " DEBUG";
#endif

    compiler += "\nCompiler __VERSION__ macro : ";
#if defined(__VERSION__)
    compiler += __VERSION__;
#else
    compiler += "(undefined macro)";
#endif
    return compiler;
}

// Used to serialize access to std::cout
// to avoid multiple threads writing at the same time.
std::ostream& operator<<(std::ostream& os, OutState out) noexcept {
    static std::mutex mutex;

    switch (out)
    {
    case OUT_LOCK :
        mutex.lock();
        break;
    case OUT_UNLOCK :
        mutex.unlock();
        break;
    }

    return os;
}

std::string format_time(std::chrono::time_point<SystemClock> timePoint) {
    static std::mutex mutex;

    std::ostringstream oss;

    auto us =
      std::chrono::duration_cast<MicroSeconds>(timePoint.time_since_epoch()).count() % 1000000;
    auto time = SystemClock::to_time_t(timePoint);
    // std::localtime is not thread safe. Since this is the only place
    // std::localtime is used in the program, guard by mutex.
    // TODO: replace with std::localtime_r or s once they are properly
    // standardised. Or some other more c++ like time component thing.
    {
        std::unique_lock uniqueLock(mutex);
        oss << std::put_time(std::localtime(&time), "%Y.%m.%d-%H:%M:%S")  //
            << '.' << std::setfill('0') << std::setw(6) << us;
    }

    return oss.str();
}

// Trampoline helper to avoid moving Logger to misc.h
void start_logger(const std::string& logFile) noexcept { Logger::start(logFile); }


#if !defined(NDEBUG)
// Debug functions used mainly to collect run-time statistics
namespace Debug {
namespace {

template<std::size_t N>
class Info final {
   public:
    constexpr std::atomic_int64_t& operator[](std::uint8_t index) noexcept { return data[index]; }

    void init(std::int64_t value) noexcept {
        data[0] = 0;
        for (std::size_t i = 1; i < N; ++i)
            data[i] = value;
    }

    void init(std::int64_t maxValue, std::int64_t minValue) noexcept {
        data[0] = 0;
        data[1] = maxValue;
        data[2] = minValue;
    }

   private:
    std::array<std::atomic_int64_t, N> data;
};

constexpr inline std::size_t MAX_SLOT = 32;

std::array<Info<2>, MAX_SLOT> hit;
std::array<Info<2>, MAX_SLOT> min;
std::array<Info<2>, MAX_SLOT> max;
std::array<Info<3>, MAX_SLOT> extreme;
std::array<Info<2>, MAX_SLOT> mean;
std::array<Info<3>, MAX_SLOT> stdev;
std::array<Info<6>, MAX_SLOT> correl;

}  // namespace

void init() noexcept {

    for (std::size_t i = 0; i < MAX_SLOT; ++i)
    {
        hit[i].init(0);
        min[i].init(std::numeric_limits<std::int64_t>::max());
        max[i].init(std::numeric_limits<std::int64_t>::min());
        extreme[i].init(std::numeric_limits<std::int64_t>::max(),
                        std::numeric_limits<std::int64_t>::min());
        mean[i].init(0);
        stdev[i].init(0);
        correl[i].init(0);
    }
}

void hit_on(bool cond, std::uint8_t slot) noexcept {

    ++hit[slot][0];
    if (cond)
        ++hit[slot][1];
}

void min_of(std::int64_t value, std::uint8_t slot) noexcept {

    ++min[slot][0];
    std::int64_t minValue = min[slot][1].load();
    while (minValue > value && !min[slot][1].compare_exchange_weak(minValue, value))
    {}
}

void max_of(std::int64_t value, std::uint8_t slot) noexcept {

    ++max[slot][0];
    std::int64_t maxValue = max[slot][1].load();
    while (maxValue < value && !max[slot][1].compare_exchange_weak(maxValue, value))
    {}
}

void extreme_of(std::int64_t value, std::uint8_t slot) noexcept {

    ++extreme[slot][0];
    std::int64_t minValue = extreme[slot][1].load();
    while (minValue > value && !extreme[slot][1].compare_exchange_weak(minValue, value))
    {}
    std::int64_t maxValue = extreme[slot][2].load();
    while (maxValue < value && !extreme[slot][2].compare_exchange_weak(maxValue, value))
    {}
}

void mean_of(std::int64_t value, std::uint8_t slot) noexcept {

    ++mean[slot][0];
    mean[slot][1] += value;
}

void stdev_of(std::int64_t value, std::uint8_t slot) noexcept {

    ++stdev[slot][0];
    stdev[slot][1] += value;
    stdev[slot][2] += value * value;
}

void correl_of(std::int64_t value1, std::int64_t value2, std::uint8_t slot) noexcept {

    ++correl[slot][0];
    correl[slot][1] += value1;
    correl[slot][2] += value1 * value1;
    correl[slot][3] += value2;
    correl[slot][4] += value2 * value2;
    correl[slot][5] += value1 * value2;
}

void print() noexcept {

    std::int64_t n = 1;

    auto avg = [&n](std::int64_t x) noexcept { return double(x) / n; };

    for (std::size_t i = 0; i < MAX_SLOT; ++i)
        if ((n = hit[i][0]))
            std::cerr << "Hit #" << i << ": Count " << n << " Hits " << hit[i][1]
                      << " Hit Rate (%) " << 100.0 * avg(hit[i][1]) << '\n';

    for (std::size_t i = 0; i < MAX_SLOT; ++i)
        if ((n = min[i][0]))
            std::cerr << "Min #" << i << ": Count " << n << " Min " << min[i][1] << '\n';

    for (std::size_t i = 0; i < MAX_SLOT; ++i)
        if ((n = max[i][0]))
            std::cerr << "Max #" << i << ": Count " << n << " Max " << max[i][1] << '\n';

    for (std::size_t i = 0; i < MAX_SLOT; ++i)
        if ((n = extreme[i][0]))
            std::cerr << "Extreme #" << i << ": Count " << n  //
                      << " Min " << extreme[i][1] << " Max " << extreme[i][2] << '\n';

    for (std::size_t i = 0; i < MAX_SLOT; ++i)
        if ((n = mean[i][0]))
            std::cerr << "Mean #" << i << ": Count " << n  //
                      << " Mean " << avg(mean[i][1]) << '\n';

    for (std::size_t i = 0; i < MAX_SLOT; ++i)
        if ((n = stdev[i][0]))
        {
            double r = std::sqrt(avg(stdev[i][2]) - sqr(avg(stdev[i][1])));
            std::cerr << "Stdev #" << i << ": Count " << n << " Stdev " << r << '\n';
        }

    for (std::size_t i = 0; i < MAX_SLOT; ++i)
        if ((n = correl[i][0]))
        {
            double r = (avg(correl[i][5]) - avg(correl[i][1]) * avg(correl[i][3]))
                     / (std::sqrt(avg(correl[i][2]) - sqr(avg(correl[i][1])))
                        * std::sqrt(avg(correl[i][4]) - sqr(avg(correl[i][3]))));
            std::cerr << "Correl #" << i << ": Count " << n << " Coefficient " << r << '\n';
        }
}
}  // namespace Debug
#endif

#if defined(_WIN32)
    #include <direct.h>
    #define GETCWD _getcwd
#else
    #include <unistd.h>
    #define GETCWD getcwd
#endif

// Extract the binary directory path
std::string CommandLine::get_binary_directory(const std::string& path) noexcept {

#if defined(_WIN32)
    std::string pathSeparator = "\\";
    #if defined(_MSC_VER)
    // Under windows path may not have the extension.
    // Also _get_pgmptr() had issues in some Windows 10 versions,
    // so check returned values carefully.
    char* pgmptr = nullptr;
    if (!_get_pgmptr(&pgmptr) && pgmptr != nullptr && *pgmptr)
        path = pgmptr;
    #endif
#else
    std::string pathSeparator = "/";
#endif

    std::string binaryDirectory = path;

    std::size_t pos = binaryDirectory.find_last_of("\\/");
    if (pos == std::string::npos)
        binaryDirectory = "." + pathSeparator;
    else
        binaryDirectory.resize(pos + 1);

    // Pattern replacement: "./" at the start of path is replaced by the working directory
    if (binaryDirectory.find("." + pathSeparator) == 0)
        binaryDirectory.replace(0, 1, CommandLine::get_working_directory());

    return binaryDirectory;
}
// Extract the working directory
std::string CommandLine::get_working_directory() noexcept {
    std::string workingDirectory;

    constexpr std::size_t BUFF_SIZE = 4096;

    char  buffer[BUFF_SIZE];
    char* cwd = GETCWD(buffer, BUFF_SIZE);
    if (cwd)
        workingDirectory = cwd;

    return workingDirectory;
}

std::size_t str_to_size_t(const std::string& str) noexcept {
    unsigned long long value = std::stoull(str);
    if (value > std::numeric_limits<std::size_t>::max())
        std::exit(EXIT_FAILURE);
    return static_cast<std::size_t>(value);
}

std::optional<std::string> read_file_to_string(const std::string& filePath) noexcept {
    std::ifstream ifstream(filePath, std::ios_base::binary);
    if (!ifstream)
        return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(ifstream), std::istreambuf_iterator<char>());
}

}  // namespace DON