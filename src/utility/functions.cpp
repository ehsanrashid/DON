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

#include "../utility/functions.h"

#include <algorithm>  // min()/max(), transform(), remove_if()
#include <charconv>   // from_chars(), from_chars_result()
#include <chrono>     // chrono::duration_cast
#include <cinttypes>  // PRIX32, PRIX64, PRIu64
#include <cstdio>     // snprintf()
#include <ctime>      // time_t, localtime_r(), localtime_s(), strftime()
#include <cwchar>     // tm
#include <fstream>
#include <limits>        // numeric_limits<>
#include <system_error>  // errc

#if defined(_WIN32)
    #include "../utility/platform_win.h"  // WideCharToMultiByte()
#endif

#include "../utility/reader.h"
#include "../utility/types.h"

namespace DON {

std::string format_date(const std::string_view date) noexcept {
    constexpr std::string_view NullDate{"00000000"};

    // Tokenize: expect "Mon DD YYYY" where DD may have a trailing comma.
    // Format from compiler: "Sep 02 2008"

    if (date.size() < 8)
        return std::string{NullDate};

    StringReader reader{date};

    // Parse month (first 3 chars)
    const Array<char, 3> monthChars{reader.get(), reader.get(), reader.get()};
    const u32            month = to_month(std::string_view{monthChars.data(), monthChars.size()});

    if (month == 0)
        return std::string{NullDate};

    // Parse day
    reader.skip_spaces();

    int day;
    if (!reader.get_int(day))
        return std::string{NullDate};

    day = constexpr_abs(day);

    if (day < 1 || 31 < day)
        return std::string{NullDate};

    // Skip spaces and optional comma
    reader.skip_spaces();

    if (reader.peek() == ',')
        reader.advance();

    // Parse year
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

std::vector<std::string_view>
split(const std::string_view sv, const std::string_view delimiter, bool trimPart) noexcept {
    // Return no parts for empty input or delimiter
    if (sv.empty() || delimiter.empty())
        return {};

    std::vector<std::string_view> parts;

    for (usize pos = 0;;)
    {
        const usize next = sv.find(delimiter, pos);

        auto part = sv.substr(pos, next - pos);

        if (trimPart)
            part = trim(part);

        parts.push_back(part);

        if (next == std::string_view::npos)
            break;

        pos = next + delimiter.size();
    }

    return parts;
}

std::vector<std::string> to_strings(const std::vector<std::string_view>& svs) noexcept {
    std::vector<std::string> strs;
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

std::string wstring_to_utf8(const std::wstring_view wsv) noexcept {
#if defined(_WIN32)
    if (wsv.empty())
        return {};

    constexpr UINT   codePage       = CP_UTF8;
    constexpr DWORD  flags          = 0;
    const LPCWCH     wideCharStr    = wsv.data();
    const int        wideCharCount  = int(wsv.size());
    constexpr LPSTR  multiByteStr   = nullptr;
    constexpr int    multiByteCount = 0;
    constexpr LPCCH  defaultChar    = nullptr;
    constexpr LPBOOL useDefaultChar = nullptr;

    const int strByteCount = ::WideCharToMultiByte(codePage, flags,               //
                                                   wideCharStr, wideCharCount,    //
                                                   multiByteStr, multiByteCount,  //
                                                   defaultChar, useDefaultChar);
    if (strByteCount <= 0)
        return {};

    std::string str(usize(strByteCount), '\0');

    const int writtenByteCount = ::WideCharToMultiByte(codePage, flags,             //
                                                       wideCharStr, wideCharCount,  //
                                                       str.data(), strByteCount,    //
                                                       defaultChar, useDefaultChar);
    if (writtenByteCount != strByteCount)
        return {};

    return str;
#else
    return std::string{wsv.begin(), wsv.end()};
#endif
}

fs::path utf8_to_path(const std::string_view utf8) noexcept {
#if defined(_WIN32)
    if (utf8.size() > usize(std::numeric_limits<int>::max()))
        return {};

    const LPCCH multiByteStr   = utf8.data();
    const int   multiByteCount = int(utf8.size());

    // First attempt UTF-8, then fall back to ANSI for old GUIs like Arena
    constexpr Array<UINT, 2> CodePages{CP_UTF8, CP_ACP};

    for (const UINT codePage : CodePages)
    {
        const DWORD flags = codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0;

        const int wideCharCount = ::MultiByteToWideChar(codePage, flags,               //
                                                        multiByteStr, multiByteCount,  //
                                                        nullptr, 0);
        if (wideCharCount <= 0)
            continue;

        std::wstring widePath(usize(wideCharCount), L'\0');

        const int writtenCharCount = ::MultiByteToWideChar(codePage, flags,               //
                                                           multiByteStr, multiByteCount,  //
                                                           widePath.data(), wideCharCount);
        if (writtenCharCount != wideCharCount)
            continue;

        return {widePath};
    }
#endif
    return {utf8};
}

#if defined(_WIN32)
std::string error_to_string(const DWORD errorId) noexcept {
    if (errorId == 0)
        return {};

    LPSTR buffer = nullptr;
    // Ask Win32 to give us the string version of that message ID.
    // The parameters pass in, tell Win32 to create the buffer that holds the message
    // (because don't yet know how long the message string will be).
    const usize size = ::FormatMessage(
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

bool is_valid_handle(const HANDLE handle) noexcept {
    return handle != HANDLE_INVALID && handle != INVALID_HANDLE_VALUE;
}

#else
bool is_valid_fd(const int fd) noexcept { return fd > FD_INVALID; }

#endif

}  // namespace DON
