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

#ifndef UTILITY_FUNCTIONS_H_INCLUDED
#define UTILITY_FUNCTIONS_H_INCLUDED

#include <optional>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
    #include "../utility/platform_win.h"
#endif

#include "../utility/types.h"

namespace DON {

inline constexpr std::string_view EMPTY_STRING{"<empty>"};
inline constexpr std::string_view WHITE_SPACE{" \t\n\v\f\r"};

[[nodiscard]] constexpr bool is_idigit(const int dg) noexcept { return 0 <= dg && dg <= 9; }
[[nodiscard]] constexpr bool is_cdigit(const char ch) noexcept { return '0' <= ch && ch <= '9'; }
[[nodiscard]] constexpr bool is_space(const char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\v' || ch == '\f' || ch == '\r';
}
[[nodiscard]] constexpr bool is_lower(const char ch) noexcept { return 'a' <= ch && ch <= 'z'; }
[[nodiscard]] constexpr bool is_upper(const char ch) noexcept { return 'A' <= ch && ch <= 'Z'; }

[[nodiscard]] constexpr char lower_case(const char ch) noexcept {
    return is_upper(ch) ? char(ch + ('a' - 'A')) : ch;
}
[[nodiscard]] constexpr char upper_case(const char ch) noexcept {
    return is_lower(ch) ? char(ch - ('a' - 'A')) : ch;
}

[[nodiscard]] constexpr char digit_to_char(const int dg) noexcept {
    assert(is_idigit(dg) && "digit_to_char: non-digit integer");

    return is_idigit(dg) ? dg + '0' : '\0';
}
[[nodiscard]] constexpr int char_to_digit(const char ch) noexcept {
    assert(is_cdigit(ch) && "char_to_digit: non-digit character");

    return is_cdigit(ch) ? ch - '0' : 0;
}

[[nodiscard]] constexpr u32 to_month(const std::string_view mon) noexcept {
    assert(mon.size() == 3);
    return lower_case(mon[0]) == 'j' && lower_case(mon[1]) == 'a' ? 1
         : lower_case(mon[0]) == 'f'                              ? 2
         : lower_case(mon[0]) == 'm' && lower_case(mon[2]) == 'r' ? 3
         : lower_case(mon[0]) == 'a' && lower_case(mon[1]) == 'p' ? 4
         : lower_case(mon[0]) == 'm' && lower_case(mon[2]) == 'y' ? 5
         : lower_case(mon[0]) == 'j' && lower_case(mon[2]) == 'n' ? 6
         : lower_case(mon[0]) == 'j' && lower_case(mon[2]) == 'l' ? 7
         : lower_case(mon[0]) == 'a' && lower_case(mon[1]) == 'u' ? 8
         : lower_case(mon[0]) == 's'                              ? 9
         : lower_case(mon[0]) == 'o'                              ? 10
         : lower_case(mon[0]) == 'n'                              ? 11
         : lower_case(mon[0]) == 'd'                              ? 12
                                                                  : 0;
}

// Tomohiko Sakamoto's Algorithm
[[nodiscard]] constexpr std::string_view
week_day(const u32 year, const u32 month, const u32 day) noexcept {
    constexpr Array<std::string_view, 7> Weekdays{
      "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"  //
    };

    // Precomputed weekday offsets for each month.
    constexpr Array<u16, 12> MonthWeekdays{0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

    // Treat January and February as part of the previous year.
    const u32 yr = year - u32(month < 3);

    // Apply the mathematical congruence formula
    const usize weekDay = (yr + yr / 4 - yr / 100 + yr / 400 + MonthWeekdays[month - 1] + day)  //
                        % Weekdays.size();

    return Weekdays[weekDay];
}

[[nodiscard]] constexpr unsigned two_digits(const char* p) noexcept {
    return 10 * char_to_digit(p[0]) + char_to_digit(p[1]);
}

// Format date "Mon DD YYYY" -> YYYYMMDD
[[nodiscard]] std::string format_date(std::string_view date) noexcept;
// Format time HH:MM:SS -> HHMMSS
[[nodiscard]] std::string format_time(std::string_view time) noexcept;

[[nodiscard]] constexpr bool starts_with(const std::string_view sv,
                                         const std::string_view prefix) noexcept {
    return sv.size() >= prefix.size()  //
        && sv.compare(0, prefix.size(), prefix) == 0;
}

[[nodiscard]] constexpr bool ends_with(const std::string_view sv,
                                       const std::string_view suffix) noexcept {
    return sv.size() >= suffix.size()  //
        && sv.compare(sv.size() - suffix.size(), suffix.size(), suffix) == 0;
}

[[nodiscard]] constexpr bool is_whitespace(const std::string_view sv) noexcept {
    return sv.find_first_not_of(WHITE_SPACE) == std::string_view::npos;
}

[[nodiscard]] constexpr std::string_view ltrim(const std::string_view sv) noexcept {
    // Find the first non-whitespace character
    const auto beg = sv.find_first_not_of(WHITE_SPACE);

    if (beg == std::string_view::npos)
        return {};

    return sv.substr(beg);
}

[[nodiscard]] constexpr std::string_view rtrim(const std::string_view sv) noexcept {
    // Find the last non-whitespace character
    const auto end = sv.find_last_not_of(WHITE_SPACE);

    if (end == std::string_view::npos)
        return {};

    return sv.substr(0, end + 1);
}

[[nodiscard]] constexpr std::string_view trim(const std::string_view sv) noexcept {
    const auto beg = sv.find_first_not_of(WHITE_SPACE);

    if (beg == std::string_view::npos)
        return {};

    const auto end = sv.find_last_not_of(WHITE_SPACE);

    return sv.substr(beg, end - beg + 1);
}

[[nodiscard]] constexpr std::string_view bool_to_str(const bool b) noexcept {
    return b ? "true" : "false";
}

[[nodiscard]] constexpr bool str_to_bool(const std::string_view sv) {
    return (trim(sv) == bool_to_str(true));
}

[[nodiscard]] constexpr int str_to_int(const std::string_view sv) noexcept {
    const char* p   = sv.data();
    const char* end = p + sv.size();

    const bool neg = p != end && *p == '-';
    if (p != end && (*p == '+' || *p == '-'))
        ++p;

    int val = 0;
    for (; p != end; ++p)
        val = 10 * val + char_to_digit(*p);

    return neg ? -val : val;
}

std::string lower_case(std::string str) noexcept;

std::string upper_case(std::string str) noexcept;

std::string toggle_case(std::string str) noexcept;

std::string remove_whitespace(std::string str) noexcept;

// Validate boolean string (case-insensitive)
bool str_is_bool(std::string_view sv) noexcept;

bool str_in_range(std::string_view sv, int minValue, int maxValue) noexcept;

std::vector<std::string_view>
split(std::string_view sv, std::string_view delimiter, bool trimPart = false) noexcept;

std::vector<std::string> to_strings(const std::vector<std::string_view>& svs) noexcept;

[[nodiscard]] std::string usize_to_hex(usize value) noexcept;

[[nodiscard]] std::string u64_to_hex(u64 value) noexcept;

[[nodiscard]] std::string u32_to_hex_prefix(u32 value) noexcept;

[[nodiscard]] std::string u64_to_hex_prefix(u64 value) noexcept;

std::optional<usize> str_to_usize(std::string_view sv) noexcept;

// Reads the file as bytes.
// Returns std::nullopt if the file does not exist.
std::optional<std::string> read_file_to_string(const fs::path& filePath) noexcept;

// Convert a wide string to UTF-8.
[[nodiscard]] std::string wstring_to_utf8(std::wstring_view wsv) noexcept;

[[nodiscard]] fs::path utf8_to_path(std::string_view utf8) noexcept;

#if defined(_WIN32)
inline constexpr HANDLE HANDLE_INVALID   = nullptr;
inline constexpr void*  MMAP_PTR_INVALID = nullptr;

// Get the error message string, if any
[[nodiscard]] std::string error_to_string(DWORD errorId) noexcept;

[[nodiscard]] bool is_valid_handle(HANDLE handle) noexcept;
#else
inline constexpr int FD_INVALID = -1;

inline constexpr void* MMAP_PTR_INVALID  = nullptr;
inline constexpr usize MMAP_SIZE_INVALID = 0;

[[nodiscard]] bool is_valid_fd(int fd) noexcept;

#endif

}  // namespace DON

#endif  // UTILITY_FUNCTIONS_H_INCLUDED
