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

#if defined(_WIN32)
    #include <shellapi.h>  // CommandLineToArgvW()
#else
    #include <sys/mman.h>  // munmap()
    #include <unistd.h>    // close(), read()/write(), unlink(), sleep(), getpid()
#endif

namespace DON {

namespace {

constexpr std::string_view NAME{"DON"};
constexpr std::string_view AUTHOR{"Ehsan Rashid"};
constexpr std::string_view VERSION{"dev"};

std::string
compiler_version(const unsigned major, const unsigned minor, const unsigned patch) noexcept {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

}  // namespace

void set_console_input(const ConsoleMode consoleMode) noexcept {
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

void set_console_output(const ConsoleMode consoleMode) noexcept {
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

// Format date "Mon DD YYYY" -> YYYYMMDD
std::string format_date(const std::string_view date) noexcept {
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
    for (; p != end && is_space(*p); ++p)
    {}

    // Parse day (1-2 digits)
    if (end - p < 1 || !is_cdigit(*p))
        return std::string{NullDate};

    unsigned day = 0;
    for (; p != end && is_cdigit(*p); ++p)
        day = 10 * day + char_to_digit(*p);

    // Validate day range
    if (day < 1 || day > 31)
        return std::string{NullDate};

    // Skip spaces/comma
    for (; p != end && (is_space(*p) || *p == ','); ++p)
    {}

    // Parse year (4 digits)
    if (end - p < 4)
        return std::string{NullDate};

    unsigned year = 0;
    for (const auto* yEnd = p + 4; p != yEnd; ++p)
    {
        if (!is_cdigit(*p))
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
std::string format_time(const std::string_view time) noexcept {
    constexpr std::string_view NullTime{"000000"};

    // Expect exactly "HH:MM:SS"
    if (time.size() != 8)
        return std::string{NullTime};

    const auto* const p = time.data();

    // Validate structure
    if (!is_cdigit(p[0]) || !is_cdigit(p[1]) || p[2] != ':'     //
        || !is_cdigit(p[3]) || !is_cdigit(p[4]) || p[5] != ':'  //
        || !is_cdigit(p[6]) || !is_cdigit(p[7]))
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

std::string build_date() noexcept {
    return
#if defined(BUILD_DATE)
      BUILD_DATE
#else
      format_date(__DATE__)
#endif
      ;
}

std::string build_time() noexcept {
    return
#if defined(BUILD_TIME)
      BUILD_TIME
#else
      format_time(__TIME__)
#endif
      ;
}

std::string build_timestamp() noexcept {
    return
#if defined(BUILD_TIMESTAMP)
      BUILD_TIMESTAMP
#else
      __DATE__ " " __TIME__
#endif
      ;
}

std::string engine_info(const bool uci) noexcept {
    std::string info;
    info.reserve(64);

    info  //
      .append(uci ? "id name " : "")
      .append(version_info())
      .append(uci ? "\nid author " : " by ")
      .append(AUTHOR);

    return info;
}

std::string engine_logo() noexcept {
    std::string logo;
    logo.reserve(1100);

    auto border = [&logo](const std::string_view sv) {
        logo += ConsoleColor::BG_BLACK;
        logo += ConsoleColor::BRIGHT_YELLOW;
        logo += ConsoleColor::BLINK;
        logo += sv;
        logo += ConsoleColor::RESET;
        logo += '\n';
    };
    auto mid1 = [&logo](const std::string_view sv, const char* const c1) {
        logo += ConsoleColor::BG_BLACK;
        logo += ConsoleColor::BRIGHT_YELLOW;
        logo += ConsoleColor::BLINK;
        logo += "  ║";
        logo += ConsoleColor::RESET;
        logo += ConsoleColor::BG_BLACK;
        logo += c1;
        logo += sv;
        logo += ConsoleColor::RESET;
        logo += ConsoleColor::BG_BLACK;
        logo += ConsoleColor::BRIGHT_YELLOW;
        logo += ConsoleColor::BLINK;
        logo += "║  ";
        logo += ConsoleColor::RESET;
        logo += '\n';
    };
    auto mid2 = [&logo](const std::string_view sv, const char* const c1, const char* const c2) {
        logo += ConsoleColor::BG_BLACK;
        logo += ConsoleColor::BRIGHT_YELLOW;
        logo += ConsoleColor::BLINK;
        logo += "  ║";
        logo += ConsoleColor::RESET;
        logo += ConsoleColor::BG_BLACK;
        logo += c1;
        logo += c2;
        logo += sv;
        logo += ConsoleColor::RESET;
        logo += ConsoleColor::BG_BLACK;
        logo += ConsoleColor::BRIGHT_YELLOW;
        logo += ConsoleColor::BLINK;
        logo += "║  ";
        logo += ConsoleColor::RESET;
        logo += '\n';
    };

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

    return logo;
}

// Returns the full human-readable DON version string.
//
// Development builds:
//   • If Git metadata is available, append commit information:
//       DON dev-YYYYMMDD-SHA
//
//   • If Git metadata is unavailable (e.g. local/source builds),
//     fall back to a timestamp-based identifier:
//       DON dev-YYYYMMDD-HHMMSS
//
// Release builds:
//   • Only include the semantic version number:
//       DON X.Y (version)
std::string version_info() noexcept {
    std::string version;
    version.reserve(32);

    version.append(NAME).append(" ").append(VERSION);

    if constexpr (VERSION == "dev")
    {
        version.push_back('-');
#if defined(GIT_DATE)
        version.append(GIT_DATE);
#else
        version.append(build_date());
#endif
        version.push_back('-');
#if defined(GIT_SHA)
        version.append(GIT_SHA);
#else
        version.append(build_time());
#endif
#if defined(GIT_DIFFINDEX)
        version.push_back('-');
        version.append(GIT_DIFFINDEX);
#endif
    }

    return version;
}

// Returns a string trying to describe the compiler used
std::string compiler_info() noexcept {
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

    compiler.append("\nCompiled by                : ");
#if defined(__clang__)
    compiler  //
      .append("clang++ ")
      .append(compiler_version(__clang_major__, __clang_minor__, __clang_patchlevel__));
#elif defined(__GNUC__)
    compiler  //
      .append("g++ (GNUC) ")
      .append(compiler_version(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__));
#elif defined(_MSC_VER)
    compiler  //
      .append("MSVC ")
      .append(compiler_version(_MSC_VER / 100, _MSC_VER % 100, _MSC_FULL_VER % 100000))
    #if defined(_MSC_BUILD)
      .append(".")
      .append(std::to_string(_MSC_BUILD))
    #endif
      ;
#elif defined(__INTEL_LLVM_COMPILER)
    compiler  //
      .append("ICX ")
    #if __INTEL_LLVM_COMPILER < 1000000L
      .append(STRINGIFY(__INTEL_LLVM_COMPILER))
    #else
      .append(compiler_version(__INTEL_LLVM_COMPILER / 10000,        //
                               (__INTEL_LLVM_COMPILER / 100) % 100,  //
                               __INTEL_LLVM_COMPILER % 100))
    #endif
      ;
#elif defined(__e2k__) && defined(__LCC__)
    compiler  //
      .append("MCST LCC ")
      .append(compiler_version(__LCC__ / 100, __LCC__ % 100, __LCC_MINOR__));
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
#elif defined(_WIN64)
    compiler.append("Microsoft Windows 64-bit");
#elif defined(_WIN32)
    compiler.append("Microsoft Windows 32-bit");
#else
    compiler.append("(unknown system)");
#endif

    compiler.append("\nCompilation architecture   : ");
#if defined(ARCH)
    compiler.append(ARCH);
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
    compiler.append(" AVX-512-ICL");
#endif
#if defined(USE_AVXVNNI)
    compiler.append(" AVX-VNNI");
#endif
#if defined(USE_VNNI)
    compiler.append(" VNNI");
#endif
#if defined(USE_AVX512)
    compiler.append(" AVX-512");
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
    compiler.append(" SSE4.1");
#endif
#if defined(USE_SSSE3)
    compiler.append(" SSSE3");
#endif
#if defined(USE_SSE2)
    compiler.append(" SSE2");
#endif
#if defined(USE_LASX)
    compiler.append(" LASX");
#endif
#if defined(USE_LSX)
    compiler.append(" LSX");
#endif
#if defined(USE_NEON_DOTPROD)
    compiler.append(" NEON-DOTPROD");
#endif
#if defined(USE_NEON)
    compiler.append(" NEON");
#endif
#if defined(USE_RVV)
    compiler.append(" RVV");
#endif
#if defined(USE_POPCNT)
    compiler.append(" POPCNT");
#endif
#if defined(NO_TABLEBASES)
    compiler.append(" NO-TABLEBASES");
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

    return compiler;
}

std::string format_time(const SystemClock::time_point& timePoint) noexcept {

    std::time_t time = SystemClock::to_time_t(timePoint);
    u64 usec = std::chrono::duration_cast<Us>(timePoint.time_since_epoch()).count() % 1000000;

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
    writtenSize +=
      std::snprintf(buffer.data() + writtenSize, buffer.size() - writtenSize, ".%06" PRIu64, usec);
    return std::string{buffer.data(), std::min(writtenSize, buffer.size() - 1)};
}

// OstreamMutexRegistry
//
// Provides a thread-safe registry that associates a unique mutex with each
// std::ostream pointer.
//
// The registry allows multiple threads to synchronize access to the same
// ostream without unnecessarily locking unrelated ostreams.
//
// Key Features:
//  - Thread-safe: registry access is protected by a mutex.
//  - Per-ostream mutex: each ostream has its own mutex to minimize contention.
//  - Lazy initialization: mutexes are created when first requested.
//  - Null-safe: nullptr is treated as a valid key and maps to a shared mutex.
//
// Usage:
//  - Call 'get(&std::cout)' to obtain the mutex before writing to std::cout
//    from multiple threads.
//  - Lock the returned mutex with std::scoped_lock or std::unique_lock.
//
// Notes:
//  - The registry does not own the std::ostream objects.
//  - Mutexes remain in the registry for the lifetime of the process.
namespace OstreamMutexRegistry {

namespace {

// Protects access to the mutex registry container.
std::mutex Mutex;

// Associates each ostream pointer with its mutex.
std::unordered_map<std::ostream*, std::mutex> MutexMap;

}  // namespace

// Returns the mutex associated with the given ostream pointer.
//
// A nullptr pointer is treated as a valid key and maps to a shared mutex.
std::mutex& get(std::ostream* const osPtr) noexcept {
    std::lock_guard writeLock(Mutex);

    return MutexMap[osPtr];
}

}  // namespace OstreamMutexRegistry

SyncOstream::SyncOstream(std::ostream& os) noexcept :
    osPtr(&os),
    lock(OstreamMutexRegistry::get(osPtr)) {}

SyncOstream::SyncOstream(SyncOstream&& syncOs) noexcept :
    osPtr(std::exchange(syncOs.osPtr, nullptr)),
    lock(std::move(syncOs.lock)) {}

SyncOstream& SyncOstream::operator<<(IosManip manip) & {
    assert(osPtr != nullptr && "Use of moved-from SyncOstream");

    manip(*osPtr);
    return *this;
}

SyncOstream&& SyncOstream::operator<<(IosManip manip) && {
    assert(osPtr != nullptr && "Use of moved-from SyncOstream");

    manip(*osPtr);
    return std::move(*this);
}

SyncOstream& SyncOstream::operator<<(OstreamManip manip) & {
    assert(osPtr != nullptr && "Use of moved-from SyncOstream");

    manip(*osPtr);
    return *this;
}

SyncOstream&& SyncOstream::operator<<(OstreamManip manip) && {
    assert(osPtr != nullptr && "Use of moved-from SyncOstream");

    manip(*osPtr);
    return std::move(*this);
}

SyncOstream sync_os(std::ostream& os) noexcept { return SyncOstream(os); }

// Factory method that creates a FixedText from the specified string view
FixedText FixedText::from(const std::string_view sv) noexcept { return FixedText{}.write(sv); }

FixedText& FixedText::write(const char ch) noexcept {
    assert(size() < capacity());
    if (size() >= capacity())
        return *this;

    data_[size_++] = ch;
    return *this;
}

FixedText& FixedText::write(const std::string_view sv) noexcept {
    assert(size() + sv.size() <= capacity());

    std::memcpy(end(), sv.data(), sv.size());
    size_ += static_cast<u8>(sv.size());
    return *this;
}

FixedText& FixedText::write(const int v) noexcept {
    auto [ptr, ec] = std::to_chars(end(), begin() + capacity(), v);
    assert(ec == std::errc{});
    size_ = static_cast<u8>(ptr - begin());
    return *this;
}

std::ostream& operator<<(std::ostream& os, const FixedText& fixedText) noexcept {

    os.write(fixedText.c_str(), std::streamsize(fixedText.size()));

    return os;
}

StringViewStreambuf::StringViewStreambuf(const std::string_view sv) noexcept {
    // std::streambuf requires char* for the get area.
    // The buffer is read-only; no characters are modified.
    auto* const p    = const_cast<char*>(sv.data());
    const usize size = sv.size();
    setg(p, p, p + size);  // Only GET area (reading enabled)
    // Do NOT call setp(p, p + size) - no PUT area (writing disabled)
}

MemoryStreambuf::MemoryStreambuf(char* const p, const usize size) noexcept {
    setg(p, p, p + size);  // Set GET area (reading enabled)
    setp(p, p + size);     // Set PUT area (writing enabled)
}

TieStreambuf::TieStreambuf(std::streambuf* const pB, std::streambuf* const mB) noexcept :
    pBuf(pB),
    mBuf(mB) {}

// Synchronizes both the primary and mirror buffers.
int TieStreambuf::sync() {
    int r1 = pBuf != nullptr ? pBuf->pubsync() : 0;
    int r2 = mBuf != nullptr ? mBuf->pubsync() : 0;

    return (r1 == 0 && r2 == 0) ? 0 : -1;
}

// Reads the next character from the primary buffer without consuming it.
TieStreambuf::int_type TieStreambuf::underflow() {
    if (pBuf == nullptr)
        return traits_type::eof();

    return pBuf->sgetc();
}

// Writes one character to the primary buffer and mirrors it with an output prefix.
TieStreambuf::int_type TieStreambuf::overflow(const int_type ch) {
    if (pBuf == nullptr)
        return traits_type::eof();

    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::not_eof(ch);

    int_type putCh = pBuf->sputc(traits_type::to_char_type(ch));

    if (traits_type::eq_int_type(putCh, traits_type::eof()))
        return putCh;

    return mirror_put_with_prefix(putCh, "<< ", oPreCh);
}

// Reads and consumes one character from the primary buffer, then mirrors it with an input prefix.
TieStreambuf::int_type TieStreambuf::uflow() {
    if (pBuf == nullptr)
        return traits_type::eof();

    int_type ch = pBuf->sbumpc();

    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return ch;

    return mirror_put_with_prefix(ch, ">> ", iPreCh);
}

// Writes a block to the primary buffer and mirrors the written characters with an output prefix.
std::streamsize TieStreambuf::xsputn(const char_type* const s, const std::streamsize count) {
    if (pBuf == nullptr)
        return 0;

    std::streamsize written = pBuf->sputn(s, count);

    if (mBuf != nullptr && written > 0)
    {
        if (oPreCh == '\n')
            mBuf->sputn("<< ", 3);

        mBuf->sputn(s, written);

        oPreCh = s[written - 1];
    }

    return written;
}

std::streambuf* TieStreambuf::pbuf() const noexcept { return pBuf; }

std::streambuf* TieStreambuf::mbuf() const noexcept { return mBuf; }

// Mirrors a character to the secondary buffer, adding a prefix at the start of each line.
TieStreambuf::int_type TieStreambuf::mirror_put_with_prefix(const int_type         ch,
                                                            const std::string_view prefix,
                                                            char_type&             preCh) noexcept {
    if (mBuf == nullptr)
        return traits_type::not_eof(ch);

    if (preCh == '\n')
        mBuf->sputn(prefix.data(), static_cast<std::streamsize>(prefix.size()));

    char_type c = traits_type::to_char_type(ch);
    preCh       = c;

    int_type r = mBuf->sputc(c);
    return traits_type::eq_int_type(r, traits_type::eof()) ? traits_type::eof()
                                                           : traits_type::not_eof(ch);
}

// Starts logging to the specified file.
// Returns true on success and false if the log file cannot be opened.
bool Logger::start(const std::filesystem::path& logFile) noexcept {
    std::lock_guard writeLock(instance().mutex);

    return instance().open(logFile);
}

// Stops logging, restores the original streams, and closes the log file.
void Logger::stop() noexcept {
    std::lock_guard writeLock(instance().mutex);

    instance().close();
}

// Initializes the logger with the streams to be redirected and mirrored.
Logger::Logger(std::istream& isRef, std::ostream& osRef) noexcept :
    is(isRef),
    os(osRef),
    isBuf(is.rdbuf()),
    osBuf(os.rdbuf()),
    itsBuf(is.rdbuf(), ofs.rdbuf()),
    otsBuf(os.rdbuf(), ofs.rdbuf()) {}

// Stops logging and restores the original streams.
Logger::~Logger() noexcept { close(); }

// Returns the single shared Logger instance.
Logger& Logger::instance() noexcept {
    static Logger logger(std::cin, std::cout);

    return logger;
}

// Opens the specified log file and redirects the streams through TieStreambuf.
// Caller must hold 'mutex'.
bool Logger::open(const std::filesystem::path& logFile) noexcept {
    if (filename == logFile.string() && is_open())
        return true;  // Already open

    close();

    if (logFile.empty())
        return true;

    filename = logFile.string();

    ofs.open(filename, std::ios::out | std::ios::app);

    if (!is_open())
    {
        DEBUG_LOG("Unable to open Log file: " << filename);
        return false;
    }

    write_timestamp("->");

    is.rdbuf(&itsBuf);
    os.rdbuf(&otsBuf);

    return true;
}

// Restores the original streams and closes the log file.
// Caller must hold 'mutex'.
void Logger::close() noexcept {
    if (!is_open())
        return;

    is.rdbuf(isBuf);
    os.rdbuf(osBuf);

    write_timestamp("<-");

    ofs.close();

    filename.clear();
}

// Returns true if the log file is open.
bool Logger::is_open() const noexcept { return ofs.is_open(); }

// Writes a timestamped marker to the log file.
void Logger::write_timestamp(std::string_view suffix) noexcept {
    if (!ofs)
        return;

    ofs << '[' << format_time(SystemClock::now()) << "] " << suffix << std::endl;
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
            data[i] = 0;
    }

    Info(const Info& info) noexcept {
        for (usize i = 0; i < Size; ++i)
            data[i] = info.data[i];
    }
    Info& operator=(const Info& info) noexcept {
        if (this == &info)
            return *this;

        for (usize i = 0; i < Size; ++i)
            data[i] = info.data[i];
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
    Array<RelaxedAtomic<i64>, Size> data;
};

class MinInfo final: public Info<2> {
   public:
    MinInfo() noexcept { data[1] = std::numeric_limits<i64>::max(); }
};

class MaxInfo final: public Info<2> {
   public:
    MaxInfo() noexcept { data[1] = std::numeric_limits<i64>::min(); }
};

class ExtremeInfo final: public Info<3> {
   public:
    ExtremeInfo() noexcept {
        data[1] = std::numeric_limits<i64>::max();
        data[2] = std::numeric_limits<i64>::min();
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

    ++info[0];
    if (cond)
        ++info[1];
}

void min_of(i64 value, usize slot) noexcept {
    assert(slot < min.size());
    if (slot >= min.size())
        return;
    auto& info = min[slot];

    ++info[0];
    {
        auto& mn = info[1];
        for (i64 minValue = mn; minValue > value && !mn.compare_exchange_weak(minValue, value);)
        {}
    }
}

void max_of(i64 value, usize slot) noexcept {
    assert(slot < max.size());
    if (slot >= max.size())
        return;
    auto& info = max[slot];

    ++info[0];
    {
        auto& mx = info[1];
        for (i64 maxValue = mx; maxValue < value && !mx.compare_exchange_weak(maxValue, value);)
        {}
    }
}

void extreme_of(i64 value, usize slot) noexcept {
    assert(slot < extreme.size());
    if (slot >= extreme.size())
        return;
    auto& info = extreme[slot];

    ++info[0];
    {
        auto& mn = info[1];
        for (i64 minValue = mn; minValue > value && !mn.compare_exchange_weak(minValue, value);)
        {}
    }
    {
        auto& mx = info[2];
        for (i64 maxValue = mx; maxValue < value && !mx.compare_exchange_weak(maxValue, value);)
        {}
    }
}

void mean_of(i64 value, usize slot) noexcept {
    assert(slot < mean.size());
    if (slot >= mean.size())
        return;
    auto& info = mean[slot];

    ++info[0];
    info[1] += value;
}

void stdev_of(i64 value, usize slot) noexcept {
    assert(slot < stdev.size());
    if (slot >= stdev.size())
        return;
    auto& info = stdev[slot];

    ++info[0];
    info[1] += value;
    info[2] += value * value;
}

void correl_of(i64 value1, i64 value2, usize slot) noexcept {
    assert(slot < correl.size());
    if (slot >= correl.size())
        return;
    auto& info = correl[slot];

    ++info[0];
    info[1] += value1;
    info[2] += value1 * value1;
    info[3] += value2;
    info[4] += value2 * value2;
    info[5] += value1 * value2;
}

void print() noexcept {

    i64        n;
    const auto avg = [&n = std::as_const(n)](const i64 x) noexcept { return double(x) / n; };

    for (usize i = 0; i < hit.size(); ++i)
    {
        const auto& info = hit[i];

        if ((n = info[0]) == 0)
            continue;

        i64 hits = info[1];

        std::cerr << "Hit #" << i << ": Count=" << n  //
                  << " Hits=" << hits                 //
                  << " Hit Rate (%)=" << 100 * avg(hits) << std::endl;
    }

    for (usize i = 0; i < min.size(); ++i)
    {
        const auto& info = min[i];

        if ((n = info[0]) == 0)
            continue;

        i64 minValue = info[1];

        std::cerr << "Min #" << i << ": Count=" << n  //
                  << " Min=" << minValue << std::endl;
    }

    for (usize i = 0; i < max.size(); ++i)
    {
        const auto& info = max[i];

        if ((n = info[0]) == 0)
            continue;

        i64 maxValue = info[1];

        std::cerr << "Max #" << i << ": Count=" << n  //
                  << " Max=" << maxValue << std::endl;
    }

    for (usize i = 0; i < extreme.size(); ++i)
    {
        const auto& info = extreme[i];

        if ((n = info[0]) == 0)
            continue;

        i64 minValue = info[1];
        i64 maxValue = info[2];

        std::cerr << "Extreme #" << i << ": Count=" << n  //
                  << " Min=" << minValue                  //
                  << " Max=" << maxValue << std::endl;
    }

    for (usize i = 0; i < mean.size(); ++i)
    {
        const auto& info = mean[i];

        if ((n = info[0]) == 0)
            continue;

        i64 sum = info[1];

        std::cerr << "Mean #" << i << ": Count=" << n  //
                  << " Sum=" << sum                    //
                  << " Mean=" << avg(sum) << std::endl;
    }

    for (usize i = 0; i < stdev.size(); ++i)
    {
        const auto& info = stdev[i];

        if ((n = info[0]) == 0)
            continue;

        i64 sum   = info[1];
        i64 sumSq = info[2];

        auto r = std::sqrt(avg(sumSq) - sqr(avg(sum)));

        std::cerr << "Stdev #" << i << ": Count=" << n  //
                  << " Stdev=" << r << std::endl;
    }

    for (usize i = 0; i < correl.size(); ++i)
    {
        const auto& info = correl[i];

        if ((n = info[0]) == 0)
            continue;

        i64 sumV1   = info[1];
        i64 sumSqV1 = info[2];
        i64 sumV2   = info[3];
        i64 sumSqV2 = info[4];
        i64 sumV1V2 = info[5];

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
        const usize utf8_argc = static_cast<usize>(wargc);

        utf8_arguments.reserve(utf8_argc);

        for (usize i = 0; i < utf8_argc; ++i)
            utf8_arguments.emplace_back(utf8_from_wstring(wargv[i]));

        LocalFree(wargv);

        arguments_.reserve(utf8_arguments.size());

        for (const auto& utf8_arg : utf8_arguments)
            arguments_.emplace_back(utf8_arg);
    }
    else
        set_arguments(argc, argv);
#else
    set_arguments(argc, argv);
#endif
}

// Returns the directory containing the executable, or "." if the directory is empty.
std::filesystem::path CommandLine::binary_directory(std::filesystem::path path) noexcept {
#if defined(_WIN32)
    // Prefer the executable path reported by Windows.
    // Unlike _get_wpgmptr(), this does not depend on the CRT entry-point variant.
    // Windows paths cannot exceed 32767 characters, so a fixed buffer is sufficient.
    // Falls back to path if the API fails.
    Array<WCHAR, 0x8000> filename{};
    const DWORD length = GetModuleFileNameW(nullptr, filename.data(), DWORD(filename.size()));
    if (length != 0 && length < filename.size())
        path = std::filesystem::path{filename.data(), filename.data() + length};
#endif

    const auto binaryDirectory{path.parent_path()};
    return binaryDirectory.empty() ? std::filesystem::path(".") : binaryDirectory;
}

// Returns the process's current working directory.
std::filesystem::path CommandLine::working_directory() noexcept {
    return std::filesystem::current_path();
}

const StringViews& CommandLine::arguments() const noexcept { return arguments_; }

void CommandLine::set_arguments(int argc, const char* argv[]) noexcept {
    const usize uargc = static_cast<usize>(argc);

    arguments_.reserve(uargc);

    for (usize i = 0; i < uargc; ++i)
        arguments_.emplace_back(argv[i]);  // Store a view without copying the string.
}

#if defined(_WIN32)

// Get the error message string, if any
std::string error_to_string(DWORD errorId) noexcept {
    if (errorId == 0)
        return {};

    LPSTR buffer = nullptr;
    // Ask Win32 to give us the string version of that message ID.
    // The parameters pass in, tell Win32 to create the buffer that holds the message
    // (because don't yet know how long the message string will be).
    usize size = FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, errorId, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&buffer),  // must pass pointer to buffer pointer
      0, nullptr);

    if (size == 0 || buffer == nullptr)
    {
        // FormatMessage failed; return a fallback string
        return "Unknown error: " + u32_to_string(errorId);
    }

    // Copy the error message into a std::string
    std::string message{buffer, size};
    // Trim trailing CR/LF that many system messages include
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n'))
        message.pop_back();
    // Free the Win32's string's buffer
    LocalFree(buffer);

    return message;
}

HandleGuard::HandleGuard(HANDLE& handleRef) noexcept :
    handle(handleRef) {}

HandleGuard::~HandleGuard() noexcept { reset(); }

bool HandleGuard::is_valid() const noexcept { return is_valid_handle(handle); }

void HandleGuard::reset(HANDLE newHandle) noexcept {
    if (handle != newHandle)
    {
        if (is_valid())
            CloseHandle(handle);

        handle = newHandle;
    }
}

void HandleGuard::dismiss() noexcept { handle = HANDLE_INVALID; }

MMapGuard::MMapGuard(void*& ptrRef) noexcept :
    mappedPtr(ptrRef) {}

MMapGuard::~MMapGuard() noexcept { reset(); }

bool MMapGuard::is_valid() const noexcept { return mappedPtr != MMAP_PTR_INVALID; }

void* MMapGuard::get() const noexcept { return mappedPtr; }

void MMapGuard::reset(void* newPtr) noexcept {
    if (mappedPtr != newPtr)
    {
        if (is_valid())
            UnmapViewOfFile(mappedPtr);

        mappedPtr = newPtr;
    }
}

void MMapGuard::dismiss() noexcept { mappedPtr = MMAP_PTR_INVALID; }

    #if defined(_WIN64)
Advapi::~Advapi() noexcept { free(); }

// The needed Windows API for processor groups could be missed from old Windows versions,
// so instead of calling them directly (forcing the linker to resolve the calls at compile time),
// try to load them at runtime.
bool Advapi::load() noexcept {

    hModule = GetModuleHandle(ModuleName);

    if (hModule == nullptr)
    {
        hModule = LoadLibraryEx(ModuleName, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        // Optional last resort
        if (hModule == nullptr)
            hModule = LoadLibrary(ModuleName);

        if (hModule == nullptr)
            return false;

        loaded = true;
    }

    openProcessToken = OpenProcessToken_((void (*)()) GetProcAddress(hModule, "OpenProcessToken"));

    lookupPrivilegeValue =
      LookupPrivilegeValue_((void (*)()) GetProcAddress(hModule, "LookupPrivilegeValueA"));

    adjustTokenPrivileges =
      AdjustTokenPrivileges_((void (*)()) GetProcAddress(hModule, "AdjustTokenPrivileges"));

    if (openProcessToken == nullptr || lookupPrivilegeValue == nullptr
        || adjustTokenPrivileges == nullptr)
    {
        free();

        return false;
    }

    return true;
}

void Advapi::free() noexcept {
    if (loaded)
    {
        assert(hModule != nullptr);

        FreeLibrary(hModule);

        hModule = nullptr;
        loaded  = false;
    }
}

    #endif

#else

FdGuard::FdGuard(int& fdRef) noexcept :
    fd(fdRef) {}

FdGuard::~FdGuard() noexcept { reset(); }

bool FdGuard::is_valid() const noexcept { return is_valid_fd(fd); }

int FdGuard::get() const noexcept { return fd; }

void FdGuard::reset(int newFd) noexcept {
    if (fd != newFd)
    {
        if (is_valid())
            ::close(fd);

        fd = newFd;
    }
}

void FdGuard::dismiss() noexcept { fd = FD_INVALID; }

MMapGuard::MMapGuard(void*& ptrRef, usize& sizeRef) noexcept :
    mappedPtr(ptrRef),
    mappedSize(sizeRef) {}

MMapGuard::~MMapGuard() noexcept { reset(); }

bool MMapGuard::is_valid() const noexcept { return mappedPtr != MMAP_PTR_INVALID; }

void* MMapGuard::get_ptr() const noexcept { return mappedPtr; }

usize MMapGuard::get_size() const noexcept { return mappedSize; }

void MMapGuard::reset(void* newPtr, usize newSize) noexcept {
    if (mappedPtr != newPtr)
    {
        if (is_valid())
            ::munmap(mappedPtr, mappedSize);

        mappedPtr  = newPtr;
        mappedSize = newSize;
    }
}

void MMapGuard::dismiss() noexcept {
    mappedPtr  = MMAP_PTR_INVALID;
    mappedSize = MMAP_SIZE_INVALID;
}

UniqueFd::UniqueFd(const int fdi) noexcept :
    fd{fdi} {}

UniqueFd::UniqueFd(UniqueFd&& uniqueFd) noexcept :
    fd{uniqueFd.release()} {}

UniqueFd& UniqueFd::operator=(UniqueFd&& uniqueFd) noexcept {
    if (this == &uniqueFd)
        return *this;

    reset(uniqueFd.release());

    return *this;
}

UniqueFd::~UniqueFd() noexcept { reset(); }

int UniqueFd::get() const noexcept { return fd; }

bool UniqueFd::is_valid() const noexcept { return is_valid_fd(fd); }

UniqueFd::operator bool() const noexcept { return is_valid(); }

int UniqueFd::release() noexcept { return std::exchange(fd, FD_INVALID); }

void UniqueFd::reset(int newFd) noexcept {
    if (fd != newFd)
    {
        if (is_valid())
            ::close(fd);

        fd = newFd;
    }
}

#endif

std::string u32_to_string(u32 v) noexcept {
    constexpr usize BufferSize = 2 + HEX32_SIZE + 1;  // "0x" + 8 hex + '\0'

    Array<char, BufferSize> buffer{};

    int   writtenSize = std::snprintf(buffer.data(), buffer.size(), "0x%08" PRIX32, v);
    usize copiedSize  = writtenSize > 0  //
                        ? std::min<usize>(writtenSize, buffer.size() - 1)
                        : 0;

    return std::string{buffer.data(), copiedSize};
}

std::string u64_to_string(u64 v) noexcept {
    constexpr usize BufferSize = 2 + HEX64_SIZE + 1;  // "0x" + 16 hex + '\0'

    Array<char, BufferSize> buffer{};

    int   writtenSize = std::snprintf(buffer.data(), buffer.size(), "0x%016" PRIX64, v);
    usize copiedSize  = writtenSize > 0  //
                        ? std::min<usize>(writtenSize, buffer.size() - 1)
                        : 0;

    return std::string{buffer.data(), copiedSize};
}

void print_info_string(const std::string_view infos) noexcept {

    if (InfoStrStop)
        return;

    for (const auto info : split(infos, "\n", true))
        if (!is_whitespace(info))
            std::cout << "info string " << info << '\n';
}

void terminate_on_critical_error(const std::string_view message) noexcept {
    print_info_string("CRITICAL ERROR: " + std::string{message});
    std::cout << std::endl;
    std::exit(EXIT_FAILURE);
}

std::string utf8_from_wstring(const std::wstring_view wsv) noexcept {
#if defined(_WIN32)
    if (wsv.empty())
        return {};

    const int size =
      WideCharToMultiByte(CP_UTF8, 0, wsv.data(), int(wsv.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};

    std::string str(static_cast<usize>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wsv.data(), int(wsv.size()), str.data(), size, nullptr,
                        nullptr);
    return str;
#else
    return std::string{wsv.begin(), wsv.end()};
#endif
}

std::filesystem::path path_from_utf8(const std::string_view path) noexcept {
#if defined(_WIN32)
    const usize size = path.size();
    if (size > std::numeric_limits<int>::max())
        return {};
    int u8Size = int(size);
    int wSize  = MultiByteToWideChar(CP_UTF8, 0, path.data(), u8Size, nullptr, 0);

    std::wstring wStr(static_cast<usize>(wSize), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.data(), u8Size, wStr.data(), wSize);
    return {wStr};
#else
    return {path};
#endif
}

std::optional<usize> str_to_usize(const std::string_view sv) noexcept {
    if (sv.empty() || sv[0] == '-')
        return std::nullopt;
    // Use from_chars (no allocation, fast)
    const char* p   = sv.data();
    const char* end = p + sv.size();
    // Skip spaces
    for (; p != end && is_space(*p); ++p)
    {}

    unsigned long long value = 0;
    // Parse decimal value (base 10) from string_view
    auto [ptr, ec] = std::from_chars(p, end, value, 10);
    if (ec != std::errc{} || ptr != end || value > std::numeric_limits<usize>::max())
        return std::nullopt;

    return static_cast<usize>(value);
}

// Reads the file as bytes.
// Returns std::nullopt if the file does not exist.
std::optional<std::string> read_file_to_string(const std::filesystem::path& filePath) noexcept {

    std::ifstream ifs{filePath, std::ios::binary | std::ios::ate};
    if (!ifs)
        return std::nullopt;

    const auto size = ifs.tellg();
    if (size < 0)
        return std::nullopt;

    ifs.seekg(0, std::ios::beg);
    if (!ifs)
        return std::nullopt;

    std::string str;
    str.resize(static_cast<usize>(size));

    //str.append(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    if (!ifs.read(str.data(), static_cast<std::streamsize>(size)))
        return std::nullopt;

    return str;
}

}  // namespace DON
