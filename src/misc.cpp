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
#include <system_error>  // errc

#if defined(_WIN32)
    #include <shellapi.h>  // CommandLineToArgvW()
#else
    #include <unistd.h>    // close()
    #include <sys/mman.h>  // munmap()
#endif

namespace DON {

namespace {

constexpr std::string_view NAME{"DON"};
constexpr std::string_view AUTHOR{"Ehsan Rashid"};
constexpr std::string_view VERSION{"dev"};

constexpr unsigned two_digits(const char* p) noexcept {
    return 10 * char_to_digit(p[0]) + char_to_digit(p[1]);
}

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

std::string format_date(const std::string_view date) noexcept {
    constexpr std::string_view NullDate{"00000000"};

    // Tokenize: expect "Mon DD YYYY" where DD may have a trailing comma.
    // Format from compiler: "Sep 02 2008"

    if (date.size() < 8)
        return std::string{NullDate};

    StringReader reader{date};

    // Parse month (first 3 chars).
    const Array<char, 3> monthChars{reader.get(), reader.get(), reader.get()};
    const u32            month = to_month(std::string_view{monthChars.data(), monthChars.size()});

    if (month == 0)
        return std::string{NullDate};

    // Parse day.
    reader.skip_spaces();

    int day;
    if (!reader.get_int(day))
        return std::string{NullDate};

    day = constexpr_abs(day);

    if (day < 1 || 31 < day)
        return std::string{NullDate};

    // Skip spaces and optional comma.
    reader.skip_spaces();

    if (reader.peek() == ',')
        reader.advance();

    // Parse year.
    int year;
    if (!reader.get_int(year))
        return std::string{NullDate};

    // Format YYYYMMDD manually.
    Array<char, 8> buffer{
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

    const auto hour = two_digits(p);
    const auto min  = two_digits(p + 3);
    const auto sec  = two_digits(p + 6);

    // Range validation (important)
    if (hour > 23 || min > 59 || sec > 59)
        return std::string{NullTime};

    Array<char, 6> buffer{p[0], p[1], p[3], p[4], p[6], p[7]};
    return std::string{buffer.data(), buffer.size()};
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
        logo += ConsoleColor::BG_BLACK;
        logo += ConsoleColor::BRIGHT_YELLOW;
        logo += ConsoleColor::BLINK;
        logo += sv;
        logo += ConsoleColor::RESET;
        logo += '\n';
    };
    const auto mid1 = [&logo](const std::string_view sv, const char* const c1) noexcept {
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
    const auto mid2 = [&logo](const std::string_view sv, const char* const c1, const char* const c2) noexcept {
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

CommandLine::CommandLine(const int argc, const char* const argv[]) noexcept {
#if defined(_WIN32)
    int wide_argc;
    if (LPWSTR* wide_argv = ::CommandLineToArgvW(::GetCommandLineW(), &wide_argc);  //
        wide_argv != nullptr)
    {
        const usize utf8_argc = usize(wide_argc);

        utf8_arguments.reserve(utf8_argc);

        for (usize i = 0; i < utf8_argc; ++i)
            utf8_arguments.emplace_back(wstring_to_utf8(wide_argv[i]));

        ::LocalFree(wide_argv);

        arguments_.reserve(utf8_arguments.size());

        for (const auto& utf8_argv : utf8_arguments)
            arguments_.emplace_back(std::string_view{utf8_argv});

        return;
    }
#endif

    const usize u_argc = usize(argc);

    arguments_.reserve(u_argc);

    for (usize i = 0; i < u_argc; ++i)
        arguments_.emplace_back(argv[i]);  // Store non-owning views.
}

fs::path CommandLine::binary_directory(fs::path path) noexcept {
#if defined(_WIN32)
    // Prefer the executable path reported by Windows.
    // Unlike _get_wpgmptr(), this does not depend on the CRT entry-point variant.
    // Windows paths cannot exceed 32767 characters, so a fixed buffer is sufficient.
    // Falls back to path if the API fails.
    Array<WCHAR, 0x8000> filename{};
    const DWORD length = ::GetModuleFileNameW(nullptr, filename.data(), DWORD(filename.size()));
    if (length != 0 && length < filename.size())
        path = fs::path{filename.data(), filename.data() + length};
#endif

    const auto binaryDirectory{path.parent_path()};
    return binaryDirectory.empty() ? fs::path(".") : binaryDirectory;
}

fs::path CommandLine::working_directory() noexcept { return fs::current_path(); }

const StringViews& CommandLine::arguments() const noexcept { return arguments_; }

namespace {

OsToMutexMap osToMutex(usize{32}, 0.75f);

}  // namespace

SyncOS::SyncOS(std::ostream& os) noexcept :
    osPtr(&os),
    lock(osToMutex.get(osPtr)) {}

SyncOS::SyncOS(SyncOS&& syncOs) noexcept :
    osPtr(std::exchange(syncOs.osPtr, nullptr)),
    lock(std::move(syncOs.lock)) {}

SyncOS& SyncOS::operator<<(IosManip manip) & {
    assert(osPtr != nullptr && "Use of moved-from SyncOS");

    manip(*osPtr);
    return *this;
}

SyncOS&& SyncOS::operator<<(IosManip manip) && {
    assert(osPtr != nullptr && "Use of moved-from SyncOS");

    manip(*osPtr);
    return std::move(*this);
}

SyncOS& SyncOS::operator<<(OstreamManip manip) & {
    assert(osPtr != nullptr && "Use of moved-from SyncOS");

    manip(*osPtr);
    return *this;
}

SyncOS&& SyncOS::operator<<(OstreamManip manip) && {
    assert(osPtr != nullptr && "Use of moved-from SyncOS");

    manip(*osPtr);
    return std::move(*this);
}

SyncOS sync_os(std::ostream& os) noexcept { return SyncOS(os); }


StringReader::StringReader(const std::string_view sv) noexcept :
    beg(sv.data()),
    cur(beg),
    end(beg + sv.size()) {}

char StringReader::peek() const noexcept { return cur != end ? *cur : Null; }

bool StringReader::is_not_space() const noexcept { return cur != end && !is_space(*cur); }

void StringReader::skip_spaces() noexcept {
    for (; cur != end && is_space(*cur); ++cur)
    {}
}

void StringReader::advance() noexcept {
    if (cur != end)
        ++cur;
}

char StringReader::get() noexcept { return cur != end ? *cur++ : Null; }

bool StringReader::get_int(int& out) noexcept {
    skip_spaces();

    const bool neg = cur != end && *cur == '-';
    if (cur != end && (*cur == '+' || *cur == '-'))
        ++cur;

    int        val = 0;
    const auto p   = cur;
    for (; cur != end && is_cdigit(*cur); ++cur)
        val = 10 * val + char_to_digit(*cur);

    if (p == cur)
        return false;

    out = neg ? -val : val;
    return true;
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
    std::lock_guard writeLock(instance().mutex);

    return instance().open(logPath);
}

void Logger::stop() noexcept {
    std::lock_guard writeLock(instance().mutex);

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

#if defined(_WIN32)

std::string error_to_string(const DWORD errorId) noexcept {
    if (errorId == 0)
        return {};

    LPSTR buffer = nullptr;
    // Ask Win32 to give us the string version of that message ID.
    // The parameters pass in, tell Win32 to create the buffer that holds the message
    // (because don't yet know how long the message string will be).
    const usize size = FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, errorId, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&buffer),  // must pass pointer to buffer pointer
      0, nullptr);

    // FormatMessage failed; return a fallback string
    if (buffer == nullptr || size == 0)
        return "Unknown error: " + u32_to_hex_prefix(errorId);

    // Copy the error message into a std::string
    std::string message{buffer, size};
    // Trim trailing CR/LF that many system messages include
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n'))
        message.pop_back();

    // Free the Win32's string's buffer
    ::LocalFree(buffer);

    return message;
}

HandleGuard::HandleGuard(HANDLE& handleRef) noexcept :
    handle(handleRef) {}

HandleGuard::~HandleGuard() noexcept { reset(); }

bool HandleGuard::is_valid() const noexcept { return is_valid_handle(handle); }

void HandleGuard::reset(const HANDLE newHandle) noexcept {
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

    openProcessToken = OpenProcessToken_(  //
      (void (*)())::GetProcAddress(hModule, "OpenProcessToken"));

    lookupPrivilegeValue = LookupPrivilegeValue_(  //
      (void (*)())::GetProcAddress(hModule, "LookupPrivilegeValueA"));

    adjustTokenPrivileges = AdjustTokenPrivileges_(  //
      (void (*)())::GetProcAddress(hModule, "AdjustTokenPrivileges"));

    if (openProcessToken == nullptr         //
        || lookupPrivilegeValue == nullptr  //
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

        ::FreeLibrary(hModule);
    }

    hModule = nullptr;
    loaded  = false;
}

    #endif

#else

FdGuard::FdGuard(int& fdRef) noexcept :
    fd(fdRef) {}

FdGuard::~FdGuard() noexcept { reset(); }

bool FdGuard::is_valid() const noexcept { return is_valid_fd(fd); }

int FdGuard::get() const noexcept { return fd; }

void FdGuard::reset(const int newFd) noexcept {
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

void MMapGuard::reset(void* const newPtr, const usize newSize) noexcept {
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

void UniqueFd::reset(const int newFd) noexcept {
    if (fd != newFd)
    {
        if (is_valid())
            ::close(fd);

        fd = newFd;
    }
}

#endif

std::string lower_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](const char ch) noexcept -> char { return lower_case(ch); });
    return str;
}

std::string upper_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](const char ch) noexcept -> char { return upper_case(ch); });
    return str;
}

std::string toggle_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(), [](const char ch) noexcept -> char {
        return is_lower(ch) ? upper_case(ch) : is_upper(ch) ? lower_case(ch) : ch;
    });
    return str;
}

std::string remove_whitespace(std::string str) noexcept {
    str.erase(std::remove_if(str.begin(), str.end(),
                             [](const char ch) noexcept -> bool { return is_space(ch); }),
              str.end());
    return str;
}

bool str_is_bool(const std::string_view sv) noexcept {
    // Convert to lowercase for case-insensitive comparison
    const auto str = lower_case(std::string{sv});
    return str == bool_to_str(false) || str == bool_to_str(true);
}

bool str_in_range(const std::string_view sv, const int minValue, const int maxValue) noexcept {
    constexpr int Base = 10;

    const char*       p   = sv.data();
    const char* const end = p + sv.size();
    // Skip spaces
    for (; p != end && is_space(*p); ++p)
    {}

    int value = 0;
    // Parse decimal value (base 10) from string_view
    auto [ptr, ec] = std::from_chars(p, end, value, Base);
    if (ec != std::errc{} || ptr != end)
        return false;
    // Check value is in range
    return minValue <= value && value <= maxValue;
}

StringViews
split(const std::string_view sv, const std::string_view delimiter, bool trimPart) noexcept {
    StringViews parts;

    if (sv.empty() || delimiter.empty())
        return parts;  // Avoid infinite loop for empty delimiter

    std::string_view part;

    usize offset = 0;

    while (true)
    {
        auto end = sv.find(delimiter, offset);

        if (end == std::string_view::npos)
            break;

        part = sv.substr(offset, end - offset);

        if (trimPart)
            part = trim(part);

        parts.emplace_back(part);
        offset = end + delimiter.size();
    }

    // Last part
    part = sv.substr(offset);

    if (trimPart)
        part = trim(part);

    parts.emplace_back(part);

    return parts;
}

Strings to_strings(const StringViews& svs) noexcept {
    Strings strs;
    strs.reserve(svs.size());

    for (const auto sv : svs)
        strs.emplace_back(sv);

    return strs;
}

std::string usize_to_hex(const usize value) noexcept {
    constexpr usize BufferSize = sizeof(usize) * 2 + 1;

    Array<char, BufferSize> buffer{};

    const int writtenSize =
      std::snprintf(buffer.data(), buffer.size(), "%0*zX", int(sizeof(usize) * 2), value);
    const usize copiedSize = writtenSize > 0 ? std::min(usize(writtenSize), buffer.size() - 1) : 0;

    return std::string{buffer.data(), copiedSize};
}

std::string u64_to_hex(const u64 value) noexcept {
    constexpr usize BufferSize = HEX64_SIZE + 1;  // 16 hex + '\0'

    Array<char, BufferSize> buffer{};

    const int   writtenSize = std::snprintf(buffer.data(), buffer.size(), "%016" PRIX64, value);
    const usize copiedSize  = writtenSize > 0 ? std::min(usize(writtenSize), buffer.size() - 1) : 0;

    return std::string{buffer.data(), copiedSize};
}

std::string u32_to_hex_prefix(const u32 value) noexcept {
    constexpr usize BufferSize = 2 + HEX32_SIZE + 1;  // "0x" + 8 hex + '\0'

    Array<char, BufferSize> buffer{};

    const int   writtenSize = std::snprintf(buffer.data(), buffer.size(), "0x%08" PRIX32, value);
    const usize copiedSize  = writtenSize > 0 ? std::min(usize(writtenSize), buffer.size() - 1) : 0;

    return std::string{buffer.data(), copiedSize};
}

std::string u64_to_hex_prefix(const u64 value) noexcept {
    constexpr usize BufferSize = 2 + HEX64_SIZE + 1;  // "0x" + 16 hex + '\0'

    Array<char, BufferSize> buffer{};

    const int   writtenSize = std::snprintf(buffer.data(), buffer.size(), "0x%016" PRIX64, value);
    const usize copiedSize  = writtenSize > 0 ? std::min(usize(writtenSize), buffer.size() - 1) : 0;

    return std::string{buffer.data(), copiedSize};
}

void print_info_string(const std::string_view info) noexcept {

    if (infoStopped)
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

// clang-format off

std::string wstring_to_utf8(const std::wstring_view wsv) noexcept {
#if defined(_WIN32)
    if (wsv.empty())
        return {};

    constexpr UINT codePage = CP_UTF8;

    const int strSize = ::WideCharToMultiByte(codePage, 0, wsv.data(), int(wsv.size()), nullptr, 0, nullptr, nullptr);

    if (strSize <= 0)
        return {};

    std::string str(usize(strSize), '\0');
    ::WideCharToMultiByte(codePage, 0, wsv.data(), int(wsv.size()), str.data(), strSize, nullptr, nullptr);

    return str;
#else
    return std::string{wsv.begin(), wsv.end()};
#endif
}

fs::path utf8_to_path(const std::string_view path) noexcept {
#if defined(_WIN32)
    const int pathSize = int(path.size());

    if (pathSize > std::numeric_limits<int>::max())
        return {};

    // First attempt UTF-8, then fall back to ANSI for old GUIs like Arena
    constexpr Array<UINT, 2> CodePages{CP_UTF8, CP_ACP};
    for (const UINT codePage : CodePages)
    {
        const DWORD flags        = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;
        const int   widePathSize = ::MultiByteToWideChar(codePage, flags, path.data(), pathSize, nullptr, 0);

        if (widePathSize <= 0)
            continue;

        std::wstring widePath(usize(widePathSize), L'\0');
        ::MultiByteToWideChar(codePage, 0, path.data(), pathSize, widePath.data(), widePathSize);

        return {widePath};
    }
#endif
    return {path};
}

// clang-format on

std::optional<usize> str_to_usize(const std::string_view sv) noexcept {
    constexpr int Base = 10;

    if (sv.empty() || sv[0] == '-')
        return std::nullopt;
    // Use from_chars (no allocation, fast)
    const char*       p   = sv.data();
    const char* const end = p + sv.size();
    // Skip spaces
    for (; p != end && is_space(*p); ++p)
    {}

    unsigned long long value = 0;
    // Parse decimal value (base 10) from string_view
    auto [ptr, ec] = std::from_chars(p, end, value, Base);
    if (ec != std::errc{} || ptr != end || value > std::numeric_limits<usize>::max())
        return std::nullopt;

    return usize(value);
}

std::optional<std::string> read_file_to_string(const fs::path& filePath) noexcept {

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
