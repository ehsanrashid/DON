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
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>

namespace DON {

namespace {

// Version number or dev.
constexpr std::string_view Version{"dev"};

#if !defined(GIT_DATE)
inline std::string format_date(std::string_view date) noexcept {
    //constexpr std::string_view Months{"Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec"};
    constexpr std::array<std::string_view, 12> Months{"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    std::istringstream iss{std::string(date)};  // From compiler, format is "Sep 21 2008"

    std::string month, day, year;
    iss >> month >> day >> year;

    if (iss.fail() || month.size() != 3 || day.size() > 2 || year.size() != 4)
        return "00000000";
    return (std::ostringstream{}
            << std::setfill('0')  //
            << std::setw(4) << year
            << std::setw(2)
            //<< (1 + Months.find(month) / 4)
            << (1 + std::distance(Months.begin(), std::find(Months.begin(), Months.end(), month)))
            << std::setw(2) << day)
      .str();
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
    using traits_type = std::streambuf::traits_type;
    using int_type    = traits_type::int_type;
    using char_type   = traits_type::char_type;

    Tie() noexcept = delete;
    Tie(std::streambuf* pBuf, std::streambuf* mBuf) noexcept :
        primaryBuf(pBuf),
        mirrorBuf(mBuf) {}

    int_type overflow(int_type ch) override {
        if (primaryBuf == nullptr)
            return traits_type::eof();
        return log(primaryBuf->sputc(traits_type::to_char_type(ch)), "<< ", preOutCh);
    }

    int_type underflow() override {
        if (primaryBuf == nullptr)
            return traits_type::eof();
        return primaryBuf->sgetc();
    }

    int_type uflow() override {
        if (primaryBuf == nullptr)
            return traits_type::eof();
        int_type ch = primaryBuf->sbumpc();
        if (traits_type::eq_int_type(ch, traits_type::eof()))
            return ch;
        return log(ch, ">> ", preInCh);
    }

    int sync() override {
        int r1 = primaryBuf != nullptr ? primaryBuf->pubsync() : 0;
        int r2 = mirrorBuf != nullptr ? mirrorBuf->pubsync() : 0;
        return (r1 == 0 && r2 == 0) ? 0 : -1;
    }

    std::streambuf *primaryBuf, *mirrorBuf;

   private:
    int_type log(int_type ch, std::string_view prefix, char_type& preCh) noexcept {
        if (mirrorBuf == nullptr)
            return traits_type::not_eof(ch);
        if (preCh == '\n')
            mirrorBuf->sputn(prefix.data(), std::streamsize(prefix.size()));
        return mirrorBuf->sputc(preCh = traits_type::to_char_type(ch));
    }

    char_type preOutCh = '\n';
    char_type preInCh  = '\n';
};

class Logger final {
   private:
    Logger() noexcept = delete;
    Logger(std::istream& is, std::ostream& os) noexcept :
        istream(is),
        ostream(os),
        ofstream(),
        iTie(istream.rdbuf(), ofstream.rdbuf()),
        oTie(ostream.rdbuf(), ofstream.rdbuf()) {}

    ~Logger() noexcept { start(""); }

    std::istream& istream;
    std::ostream& ostream;
    std::ofstream ofstream;
    Tie           iTie;
    Tie           oTie;

   public:
    static void start(std::string_view logFile) noexcept {
        static Logger logger(std::cin, std::cout);  // Tie std::cin and std::cout to a file.

        if (logger.ofstream.is_open())
        {
            logger.ofstream << "[" << format_time(SystemClock::now()) << "] <-" << std::endl;

            logger.istream.rdbuf(logger.iTie.primaryBuf);
            logger.ostream.rdbuf(logger.oTie.primaryBuf);
            logger.ofstream.close();
        }

        if (logFile.empty())
            return;

        logger.ofstream.open(std::string(logFile), std::ios_base::out | std::ios_base::app);

        if (!logger.ofstream.is_open())
        {
            std::cerr << "Unable to open debug log file: " << logFile << std::endl;
            std::exit(EXIT_FAILURE);
        }
        logger.ofstream << "[" << format_time(SystemClock::now()) << "] ->" << std::endl;

        logger.istream.rdbuf(&logger.iTie);
        logger.ostream.rdbuf(&logger.oTie);
    }
};

}  // namespace

std::string engine_info(bool uci) noexcept {
    return (std::ostringstream{} << (uci ? "id name " : "") << version_info()
                                 << (uci ? "\nid author " : " by ") << "Ehsan Rashid")
      .str();
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
    oss << "DON " << Version;

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

    std::ostringstream oss;

    oss << "\nCompiled by                : ";
#if defined(__INTEL_LLVM_COMPILER)
    oss << "ICX ";
    oss << STRINGIFY(__INTEL_LLVM_COMPILER);
#elif defined(__clang__)
    oss << "clang++ ";
    oss << VERSION_STRING(__clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(_MSC_VER)
    oss << "MSVC ";
    oss << STRINGIFY(_MSC_FULL_VER) "." STRINGIFY(_MSC_BUILD);
#elif defined(__GNUC__)
    oss << "g++ (GNUC) ";
    oss << VERSION_STRING(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#elif defined(__e2k__) && defined(__LCC__)
    oss << "MCST LCC ";
    oss << std::to_string(__LCC__ / 100) << "."  //
        << std::to_string(__LCC__ % 100) << "."  //
        << std::to_string(__LCC_MINOR__);
#else
    oss << "(unknown compiler)";
#endif

    oss << "\nCompiled on                : ";
#if defined(__APPLE__)
    oss << "Apple";
#elif defined(__CYGWIN__)
    oss << "Cygwin";
#elif defined(__MINGW64__)
    oss << "MinGW64";
#elif defined(__MINGW32__)
    oss << "MinGW32";
#elif defined(__ANDROID__)
    oss << "Android";
#elif defined(__linux__)
    oss << "Linux";
#elif defined(_WIN64)
    oss << "Microsoft Windows 64-bit";
#elif defined(_WIN32)
    oss << "Microsoft Windows 32-bit";
#else
    oss << "(unknown system)";
#endif

    oss << "\nCompilation architecture   : ";
#if defined(ARCH)
    oss << STRINGIFY(ARCH);
#else
    oss << "(undefined architecture)";
#endif

    oss << "\nCompilation settings       : ";
#if defined(IS_64BIT)
    oss << "64bit";
#else
    oss << "32bit";
#endif
#if defined(USE_AVX512ICL)
    oss << " AVX512ICL";
#endif
#if defined(USE_VNNI)
    oss << " VNNI";
#endif
#if defined(USE_AVX512)
    oss << " AVX512";
#endif
#if defined(USE_PEXT)
    oss << " BMI2";
#endif
#if defined(USE_AVX2)
    oss << " AVX2";
#endif
#if defined(USE_SSE41)
    oss << " SSE41";
#endif
#if defined(USE_SSSE3)
    oss << " SSSE3";
#endif
#if defined(USE_SSE2)
    oss << " SSE2";
#endif
#if defined(USE_POPCNT)
    oss << " POPCNT";
#endif
#if defined(USE_NEON_DOTPROD)
    oss << " NEON_DOTPROD";
#elif defined(USE_NEON)
    oss << " NEON";
#endif

#if !defined(NDEBUG)
    oss << " DEBUG";
#endif

    oss << "\nCompiler __VERSION__ macro : ";
#if defined(__VERSION__)
    oss << __VERSION__;
#else
    oss << "(undefined macro)";
#endif

#undef VERSION_STRING

    return oss.str();
}

std::string format_time(const SystemClock::time_point& timePoint) {
    auto time = SystemClock::to_time_t(timePoint);
    auto usec =
      std::chrono::duration_cast<MicroSeconds>(timePoint.time_since_epoch()).count() % 1000000;
    return (std::ostringstream{} << std::put_time(std::localtime(&time), "%Y.%m.%d-%H:%M:%S") << '.'
                                 << std::setfill('0') << std::setw(6) << usec)
      .str();
}

// Trampoline helper to avoid moving Logger to misc.h
void start_logger(std::string_view logFile) noexcept { Logger::start(logFile); }

#if !defined(NDEBUG)
// Debug functions used mainly to collect run-time statistics
namespace Debug {
namespace {

template<std::size_t N>
class Info final {
   public:
    constexpr Info& operator=(const Info& info) noexcept {
        for (std::size_t i = 0; i < N; ++i)
            data[i].store(info.data[i].load());
        return *this;
    }

    [[nodiscard]] constexpr std::atomic<std::int64_t>&  //
    operator[](std::size_t index) const noexcept {
        return data[index];
    }
    [[nodiscard]] constexpr std::atomic<std::int64_t>&  //
    operator[](std::size_t index) noexcept {
        return data[index];
    }

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
    std::array<std::atomic<std::int64_t>, N> data;
};

constexpr std::size_t MaxSlot = 128;

std::array<Info<2>, MaxSlot> hit;
std::array<Info<2>, MaxSlot> min;
std::array<Info<2>, MaxSlot> max;
std::array<Info<3>, MaxSlot> extreme;
std::array<Info<2>, MaxSlot> mean;
std::array<Info<3>, MaxSlot> stdev;
std::array<Info<6>, MaxSlot> correl;

}  // namespace

void init() noexcept {
    for (std::size_t i = 0; i < MaxSlot; ++i)
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

void hit_on(bool cond, std::size_t slot) noexcept {

    hit.at(slot)[0].fetch_add(1, std::memory_order_relaxed);
    if (cond)
        hit.at(slot)[1].fetch_add(1, std::memory_order_relaxed);
}

void min_of(std::int64_t value, std::size_t slot) noexcept {

    min.at(slot)[0].fetch_add(1, std::memory_order_relaxed);
    auto minValue = min.at(slot)[1].load(std::memory_order_relaxed);
    while (minValue > value
           && !min.at(slot)[1].compare_exchange_weak(minValue, value, std::memory_order_relaxed,
                                                     std::memory_order_relaxed))
        ;
}

void max_of(std::int64_t value, std::size_t slot) noexcept {

    max.at(slot)[0].fetch_add(1, std::memory_order_relaxed);
    auto maxValue = max.at(slot)[1].load(std::memory_order_relaxed);
    while (maxValue < value
           && !max.at(slot)[1].compare_exchange_weak(maxValue, value, std::memory_order_relaxed,
                                                     std::memory_order_relaxed))
        ;
}

void extreme_of(std::int64_t value, std::size_t slot) noexcept {

    extreme.at(slot)[0].fetch_add(1, std::memory_order_relaxed);
    auto minValue = extreme.at(slot)[1].load(std::memory_order_relaxed);
    while (minValue > value
           && !extreme.at(slot)[1].compare_exchange_weak(minValue, value, std::memory_order_relaxed,
                                                         std::memory_order_relaxed))
        ;
    auto maxValue = extreme.at(slot)[2].load(std::memory_order_relaxed);
    while (maxValue < value
           && !extreme.at(slot)[2].compare_exchange_weak(maxValue, value, std::memory_order_relaxed,
                                                         std::memory_order_relaxed))
        ;
}

void mean_of(std::int64_t value, std::size_t slot) noexcept {

    mean.at(slot)[0].fetch_add(1, std::memory_order_relaxed);
    mean.at(slot)[1].fetch_add(value, std::memory_order_relaxed);
}

void stdev_of(std::int64_t value, std::size_t slot) noexcept {

    stdev.at(slot)[0].fetch_add(1, std::memory_order_relaxed);
    stdev.at(slot)[1].fetch_add(value, std::memory_order_relaxed);
    stdev.at(slot)[2].fetch_add(value * value, std::memory_order_relaxed);
}

void correl_of(std::int64_t value1, std::int64_t value2, std::size_t slot) noexcept {

    correl.at(slot)[0].fetch_add(1, std::memory_order_relaxed);
    correl.at(slot)[1].fetch_add(value1, std::memory_order_relaxed);
    correl.at(slot)[2].fetch_add(value1 * value1, std::memory_order_relaxed);
    correl.at(slot)[3].fetch_add(value2, std::memory_order_relaxed);
    correl.at(slot)[4].fetch_add(value2 * value2, std::memory_order_relaxed);
    correl.at(slot)[5].fetch_add(value1 * value2, std::memory_order_relaxed);
}

void print() noexcept {

    std::int64_t n;
    const auto   avg = [&n](std::int64_t x) noexcept { return double(x) / n; };

    for (std::size_t i = 0; i < MaxSlot; ++i)
        if ((n = hit[i][0]))
            std::cerr << "Hit #" << i << ": Count " << n << " Hits " << hit[i][1]
                      << " Hit Rate (%) " << 100 * avg(hit[i][1]) << std::endl;

    for (std::size_t i = 0; i < MaxSlot; ++i)
        if ((n = min[i][0]))
            std::cerr << "Min #" << i << ": Count " << n << " Min " << min[i][1] << std::endl;

    for (std::size_t i = 0; i < MaxSlot; ++i)
        if ((n = max[i][0]))
            std::cerr << "Max #" << i << ": Count " << n << " Max " << max[i][1] << std::endl;

    for (std::size_t i = 0; i < MaxSlot; ++i)
        if ((n = extreme[i][0]))
            std::cerr << "Extreme #" << i << ": Count " << n  //
                      << " Min " << extreme[i][1] << " Max " << extreme[i][2] << std::endl;

    for (std::size_t i = 0; i < MaxSlot; ++i)
        if ((n = mean[i][0]))
            std::cerr << "Mean #" << i << ": Count " << n << " Mean " << avg(mean[i][1])
                      << std::endl;

    for (std::size_t i = 0; i < MaxSlot; ++i)
        if ((n = stdev[i][0]))
        {
            auto r = std::sqrt(avg(stdev[i][2]) - sqr(avg(stdev[i][1])));
            std::cerr << "Stdev #" << i << ": Count " << n << " Stdev " << r << std::endl;
        }

    for (std::size_t i = 0; i < MaxSlot; ++i)
        if ((n = correl[i][0]))
        {
            auto r = (avg(correl[i][5]) - avg(correl[i][1]) * avg(correl[i][3]))
                   / (std::sqrt(avg(correl[i][2]) - sqr(avg(correl[i][1])))
                      * std::sqrt(avg(correl[i][4]) - sqr(avg(correl[i][3]))));
            std::cerr << "Correl #" << i << ": Count " << n << " Coefficient " << r << std::endl;
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
    if (!_get_pgmptr(&pgmptr) && pgmptr != nullptr && *pgmptr)
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

    constexpr std::size_t BuffSize = 4096;

    char  buffer[BuffSize];
    char* cwd = GETCWD(buffer, BuffSize);

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

std::streamsize get_file_size(std::ifstream& ifstream) noexcept {
    // Return -1 if the file stream is not open
    if (!ifstream.is_open())
        return std::streamsize(-1);

    // Store the current position
    auto curPos = ifstream.tellg();
    // Move to the end to get the size
    ifstream.seekg(0, std::ios_base::end);
    std::streamsize fileSize = ifstream.tellg();
    // Restore the original position
    ifstream.seekg(curPos, std::ios_base::beg);
    // Return file size or -1 if tellg() fails
    return fileSize >= 0 ? fileSize : std::streamsize(-1);
}

std::optional<std::string> read_file_to_string(std::string_view filePath) noexcept {

    std::ifstream ifstream(std::string(filePath), std::ios_base::binary);
    if (!ifstream)
        return std::nullopt;

    return std::string(std::istreambuf_iterator<char>(ifstream), std::istreambuf_iterator<char>());
}

}  // namespace DON
