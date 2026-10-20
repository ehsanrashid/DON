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

#include <charconv>      // from_chars()
#include <cinttypes>     // PRIX32, PRIX64, PRIu64
#include <cstdio>        // snprintf()
#include <cstdlib>       // exit(), EXIT_FAILURE
#include <ctime>         // time_t, localtime_r(), localtime_s(), strftime()
#include <iostream>      // cout, cin
#include <system_error>  // errc
#include <vector>        // vector<>

#if !defined(_WIN32)
    #include <unistd.h>    // close()
    #include <sys/mman.h>  // munmap()
#endif

#include "utility/color.h"
#include "utility/functions.h"

namespace DON {

namespace {

constexpr std::string_view NAME{"DON"};
constexpr std::string_view AUTHOR{"Ehsan Rashid"};
constexpr std::string_view VERSION{"dev"};

std::string compiler_version(const u32 major, const u32 minor, const u32 patch) noexcept {
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

}  // namespace

void set_console_utf8() noexcept {
#if defined(_WIN32)
    ::SetConsoleCP(CP_UTF8);
    ::SetConsoleOutputCP(CP_UTF8);
#endif
}

std::string build_date() noexcept {
#if defined(BUILD_DATE)
    return BUILD_DATE;
#else
    return format_date(__DATE__);
#endif
}

std::string build_time() noexcept {
#if defined(BUILD_TIME)
    return BUILD_TIME;
#else
    return format_time(__TIME__);
#endif
}

std::string build_timestamp() noexcept {
#if defined(BUILD_TIMESTAMP)
    return BUILD_TIMESTAMP;
#else
    const auto date    = std::string{__DATE__};
    const auto yyyy    = date.substr(7, 4);
    const auto mmm     = date.substr(0, 3);
    const auto dd1     = date[4] == ' ';
    const auto dd      = date.substr(dd1 ? 5 : 4, dd1 ? 1 : 2);
    const auto weekday = std::string{week_day(std::stoi(yyyy), to_month(mmm), std::stoi(dd))};
    const auto time    = std::string{__TIME__};

    return yyyy + " " + mmm + " " + (dd1 ? "0" : "") + dd + " " + weekday + " " + time;
#endif
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
    // clang-format off
    const auto border = [&logo](const std::string_view sv) noexcept {
        logo += Color::BG_BLACK;
        logo += Color::BRIGHT_YELLOW;
        logo += Color::BLINK;
        logo += sv;
        logo += Color::RESET;
        logo += '\n';
    };
    const auto mid1 = [&logo](const std::string_view sv, const char* const c1) noexcept {
        logo += Color::BG_BLACK;
        logo += Color::BRIGHT_YELLOW;
        logo += Color::BLINK;
        logo += "  ║";
        logo += Color::RESET;
        logo += Color::BG_BLACK;
        logo += c1;
        logo += sv;
        logo += Color::RESET;
        logo += Color::BG_BLACK;
        logo += Color::BRIGHT_YELLOW;
        logo += Color::BLINK;
        logo += "║  ";
        logo += Color::RESET;
        logo += '\n';
    };
    const auto mid2 = [&logo](const std::string_view sv, const char* const c1, const char* const c2) noexcept {
        logo += Color::BG_BLACK;
        logo += Color::BRIGHT_YELLOW;
        logo += Color::BLINK;
        logo += "  ║";
        logo += Color::RESET;
        logo += Color::BG_BLACK;
        logo += c1;
        logo += c2;
        logo += sv;
        logo += Color::RESET;
        logo += Color::BG_BLACK;
        logo += Color::BRIGHT_YELLOW;
        logo += Color::BLINK;
        logo += "║  ";
        logo += Color::RESET;
        logo += '\n';
    };

    border("  ╔══════════════════════════════╗  ");
         mid1("  ██████╗ ╔██████╗ ███╗  ██╗  ", Color::RED);
         mid2("  ██╔══██╗██╔═══██╗████╗ ██║  ", Color::BRIGHT_RED, Color::STRIKETHROUGH);
         mid2("  ██║  ██║██║   ██║██╔██╗██║  ", Color::BRIGHT_RED, Color::STRIKETHROUGH);
         mid2("  ██║  ██║██║   ██║██║╚████║  ", Color::BRIGHT_RED, Color::STRIKETHROUGH);
         mid1("  ██████╔╝╚██████╔╝██║ ╚███║  ", Color::RED);
         mid1("  ╚═════╝  ╚═════╝ ╚═╝  ╚══╝  ", Color::RED);
    border("  ╚══════════════════════════════╝  ");
    // clang-format on

    return logo;
}

std::string version_info() noexcept {
    std::string version;
    version.reserve(32);

    version.append(NAME);
    version.push_back(' ');
    version.append(VERSION);

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

std::string compiler_info() noexcept {
    // Predefined macros hell:
    //
    // __INTEL_LLVM_COMPILER    Intel oneAPI DPC++/C++ Compiler (ICX)
    // __INTEL_COMPILER         Intel C++ Compiler (ICC/ICL)
    // __clang__                Clang-based compiler: Clang or ICX
    // __GNUC__                 GCC or other GCC-compatible compiler: GCC, Clang or ICX
    // _MSC_VER                 Microsoft Visual C++ (MSVC)
    // __LCC__ + __e2k__        MCST LCC compiler targeting E2K
    // __LCC__                  MCST LCC compiler
    // __APPLE__                Apple platforms
    // __CYGWIN__               Cygwin
    // __MINGW64__              MinGW 64-bit
    // __MINGW32__              MinGW 32-bit
    // __ANDROID__              Android
    // __linux__                Linux
    // _WIN64                   Windows 64-bit
    // _WIN32                   Windows (any)

    std::string compiler;
    compiler.reserve(256);

    compiler
      .append("\nCompiled by                : ")
#if defined(__INTEL_LLVM_COMPILER)
      .append("ICX ")
    #if __INTEL_LLVM_COMPILER < 1000000L
      .append(STRINGIFY(__INTEL_LLVM_COMPILER))
    #else
      .append(compiler_version(__INTEL_LLVM_COMPILER / 10000,        //
                               (__INTEL_LLVM_COMPILER / 100) % 100,  //
                               __INTEL_LLVM_COMPILER % 100))
    #endif
#elif defined(__INTEL_COMPILER)
      .append("ICC/ICL ")
      .append(compiler_version(__INTEL_COMPILER / 100, __INTEL_COMPILER % 100, 0))
#elif defined(__clang__)
      .append("clang++ ")
      .append(compiler_version(__clang_major__, __clang_minor__, __clang_patchlevel__))
#elif defined(__GNUC__)
      .append("g++ (GNUC) ")
      .append(compiler_version(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__))
#elif defined(_MSC_VER)
      .append("MSVC ")
      .append(compiler_version(_MSC_VER / 100, _MSC_VER % 100, _MSC_FULL_VER % 100000))
    #if defined(_MSC_BUILD)
      .append(".")
      .append(std::to_string(_MSC_BUILD))
    #endif
#elif defined(__LCC__)
      .append("MCST LCC ")  // MCST LCC
    #if defined(__e2k__)
      .append("E2K ")  // MCST LCC targeting E2K
    #endif
      .append(compiler_version(__LCC__ / 100, __LCC__ % 100, __LCC_MINOR__))
#else
      .append("(unknown compiler)")
#endif
      ;

    compiler
      .append("\nCompiled on                : ")
#if defined(__APPLE__)
      .append("Apple")
#elif defined(__CYGWIN__)
      .append("Cygwin")
#elif defined(__MINGW64__)
      .append("MinGW64")
#elif defined(__MINGW32__)
      .append("MinGW32")
#elif defined(__ANDROID__)
      .append("Android")
#elif defined(__linux__)
      .append("Linux")
#elif defined(_WIN64)
      .append("Microsoft Windows 64-bit")
#elif defined(_WIN32)
      .append("Microsoft Windows 32-bit")
#else
      .append("(unknown system)")
#endif
      ;

    compiler
      .append("\nCompilation architecture   : ")
#if defined(ARCH)
      .append(ARCH)
#else
      .append("(unknown architecture)")
#endif
      ;

    compiler
      .append("\nCompilation settings       : ")
#if defined(IS_64BIT)
      .append("64-bit")
#else
      .append("32-bit")
#endif
#if defined(USE_AVX512ICL)
      .append(" AVX-512-ICL")
#endif
#if defined(USE_AVXVNNI)
      .append(" AVX-VNNI")
#endif
#if defined(USE_VNNI)
      .append(" VNNI")
#endif
#if defined(USE_AVX512)
      .append(" AVX-512")
#endif
#if defined(USE_BMI2)
      .append(" BMI2")
    #if defined(USE_CMP)
      .append("-CMP")
    #endif
#endif
#if defined(USE_AVX2)
      .append(" AVX2")
#endif
#if defined(USE_SSE41)
      .append(" SSE4.1")
#endif
#if defined(USE_SSSE3)
      .append(" SSSE3")
#endif
#if defined(USE_SSE2)
      .append(" SSE2")
#endif
#if defined(USE_LASX)
      .append(" LASX")
#endif
#if defined(USE_LSX)
      .append(" LSX")
#endif
#if defined(USE_NEON_DOTPROD)
      .append(" NEON-DOTPROD")
#endif
#if defined(USE_NEON)
      .append(" NEON")
#endif
#if defined(USE_RVV)
      .append(" RVV")
#endif
#if defined(USE_POPCNT)
      .append(" POPCNT")
#endif
#if defined(NO_TABLEBASES)
      .append(" NO-TABLEBASES")
#endif

#if !defined(NDEBUG)
      .append(" DEBUG")
#endif
      ;

    compiler
      .append("\nCompiler __VERSION__ macro : ")
#if defined(__VERSION__)
      .append(__VERSION__)
#else
      .append("(unknown macro)")
#endif
      ;

    return compiler;
}

std::string format_time(const SystemClock::time_point& timePoint) noexcept {
    constexpr i64 UsecPerSec = 1'000'000;

    const std::time_t time = SystemClock::to_time_t(timePoint);

    const auto totalUsec = std::chrono::duration_cast<Us>(timePoint.time_since_epoch()).count();
    const u64  usec      = u64((totalUsec % UsecPerSec + UsecPerSec) % UsecPerSec);

    std::tm tm{};
#if defined(_WIN32)  // Windows
    localtime_s(&tm, &time);
#elif defined(__unix__) || defined(__APPLE__)  // POSIX (Unix-like systems / macOS)
    localtime_r(&time, &tm);
#else
    // Fallback (not thread-safe)
    const auto* const localTm = std::localtime(&time);
    assert(localTm != nullptr);
    tm = *localTm;
#endif

    Array<char, 32> buffer{};

    usize writtenSize;
    // Format the date and time: YYYY.MM.DD-HH:MM:SS
    writtenSize = std::strftime(buffer.data(), buffer.size(), "%Y.%m.%d-%H:%M:%S", &tm);
    // Append microseconds
    writtenSize += usize(
      std::snprintf(buffer.data() + writtenSize, buffer.size() - writtenSize, ".%06" PRIu64, usec));

    return std::string{buffer.data(), std::min(writtenSize, buffer.size() - 1)};
}

usize CaseInsensitiveHash::operator()(const std::string_view sv) const noexcept {
    return std::hash<std::string_view>{}(lower_case(std::string{sv}));
}

bool CaseInsensitiveEqual::operator()(const std::string_view sv1,
                                      const std::string_view sv2) const noexcept {
    return sv1.size() == sv2.size()
        && std::equal(sv1.begin(), sv1.end(), sv2.begin(), sv2.end(),
                      [](const char ch1, const char ch2) noexcept {
                          return lower_case(ch1) == lower_case(ch2);
                      });
}

bool CaseInsensitiveLess::operator()(const std::string_view sv1,
                                     const std::string_view sv2) const noexcept {
    return std::lexicographical_compare(
      sv1.begin(), sv1.end(), sv2.begin(), sv2.end(),
      [](const char ch1, const char ch2) noexcept { return lower_case(ch1) < lower_case(ch2); });
}

FixedText FixedText::from(const std::string_view sv) noexcept { return FixedText{}.write(sv); }

FixedText& FixedText::write(const char ch) noexcept {
    assert(size() + 1 <= capacity());
    if (size() + 1 > capacity())
        return *this;

    data_[size_++] = ch;

    return *this;
}

FixedText& FixedText::write(const std::string_view sv) noexcept {
    assert(size() + sv.size() <= capacity());
    if (size() + sv.size() > capacity())
        return *this;

    std::memcpy(end(), sv.data(), sv.size());
    size_ += u8(sv.size());

    return *this;
}

FixedText& FixedText::write(const int v) noexcept {
    constexpr int Base = 10;

    auto [ptr, ec] = std::to_chars(end(), begin() + capacity(), v, Base);
    assert(ec == std::errc{});

    size_ = u8(ptr - begin());

    return *this;
}

std::ostream& operator<<(std::ostream& os, const FixedText& fixedText) noexcept {
    os.write(fixedText.c_str(), static_cast<std::streamsize>(fixedText.size()));

    return os;
}

StringBuf::StringBuf(const std::string_view sv) noexcept {
    // std::streambuf requires char* for the get area.
    // The buffer is read-only; no characters are modified.
    auto* const p    = const_cast<char*>(sv.data());
    const usize size = sv.size();
    setg(p, p, p + size);  // Only GET area (reading enabled)
    // Do NOT call setp(p, p + size) - no PUT area (writing disabled)
}

MemoryBuf::MemoryBuf(char* const p, const usize size) noexcept {
    setg(p, p, p + size);  // Set GET area (reading enabled)
    setp(p, p + size);     // Set PUT area (writing enabled)
}

TieBuf::TieBuf(std::streambuf* const pBf, std::streambuf* const mBf) noexcept :
    pBuf(pBf),
    mBuf(mBf) {}

int TieBuf::sync() {
    const int pR = pBuf != nullptr ? pBuf->pubsync() : 0;
    const int mR = mBuf != nullptr ? mBuf->pubsync() : 0;

    return pR == 0 && mR == 0 ? 0 : -1;
}

TieBuf::int_type TieBuf::underflow() {
    if (pBuf == nullptr)
        return traits_type::eof();

    return pBuf->sgetc();
}

TieBuf::int_type TieBuf::overflow(const int_type ch) {
    if (pBuf == nullptr)
        return traits_type::eof();

    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return traits_type::not_eof(ch);

    int_type putCh = pBuf->sputc(traits_type::to_char_type(ch));

    if (traits_type::eq_int_type(putCh, traits_type::eof()))
        return putCh;

    return mirror_put_with_prefix(putCh, "<< ", opreCh);
}

TieBuf::int_type TieBuf::uflow() {
    if (pBuf == nullptr)
        return traits_type::eof();

    int_type ch = pBuf->sbumpc();

    if (traits_type::eq_int_type(ch, traits_type::eof()))
        return ch;

    return mirror_put_with_prefix(ch, ">> ", ipreCh);
}

std::streamsize TieBuf::xsputn(const char_type* const s, const std::streamsize count) {
    if (pBuf == nullptr)
        return 0;

    const std::streamsize written = pBuf->sputn(s, count);

    if (mBuf == nullptr)
        return written;

    if (written <= 0)
        return written;

    if (opreCh == '\n')
        mBuf->sputn("<< ", 3);

    mBuf->sputn(s, written);

    opreCh = s[written - 1];

    return written;
}

std::streambuf* TieBuf::pbuf() const noexcept { return pBuf; }

std::streambuf* TieBuf::mbuf() const noexcept { return mBuf; }

TieBuf::int_type TieBuf::mirror_put_with_prefix(const int_type         ch,
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

bool Logger::start(const fs::path& logPath) noexcept {
    std::lock_guard lockGuard{instance().mutex};

    return instance().open(logPath);
}

void Logger::stop() noexcept {
    std::lock_guard lockGuard{instance().mutex};

    instance().close();
}

Logger::Logger(std::istream& isRef, std::ostream& osRef) noexcept :
    is(isRef),
    os(osRef),
    isBuf(is.rdbuf()),
    osBuf(os.rdbuf()),
    itBuf(is.rdbuf(), ofs.rdbuf()),
    otBuf(os.rdbuf(), ofs.rdbuf()) {}

Logger::~Logger() noexcept { close(); }

Logger& Logger::instance() noexcept {
    static Logger logger(std::cin, std::cout);

    return logger;
}

bool Logger::open(const fs::path& logPath) noexcept {
    if (filename == logPath.string() && is_open())
        return true;  // Already open

    close();

    if (logPath.empty())
        return true;

    filename = logPath.string();

    ofs.open(filename, std::ios::out | std::ios::app);

    if (!is_open())
    {
        DEBUG_LOG("Unable to open Log file: " << filename);
        return false;
    }

    write_timestamp("->");

    is.rdbuf(&itBuf);
    os.rdbuf(&otBuf);

    return true;
}

void Logger::close() noexcept {
    if (!is_open())
        return;

    is.rdbuf(isBuf);
    os.rdbuf(osBuf);

    write_timestamp("<-");

    ofs.close();

    filename.clear();
}

bool Logger::is_open() const noexcept { return ofs.is_open(); }

void Logger::write_timestamp(std::string_view suffix) noexcept {
    if (!ofs)
        return;

    ofs << '[' << format_time(SystemClock::now()) << "] " << suffix << std::endl;
}

void print_info_string(const std::string_view info) noexcept {

    if (InfoStopped)
        return;

    for (const auto line : split(info, "\n", true))
        if (!is_whitespace(line))
            std::cout << "info string " << line << '\n';
}

void terminate_on_critical_error(const std::string_view message) noexcept {
    print_info_string("CRITICAL ERROR: " + std::string{message});
    std::cout << std::endl;
    std::exit(EXIT_FAILURE);
}

}  // namespace DON
