#pragma once

#include <cctype>
#include <algorithm>
#include <string>
#include <string_view>

constexpr std::string_view Blanks{ " \f\n\r\t\v" };

inline bool whiteSpaces(std::string_view str) {
    return str.empty()
        || std::all_of(str.begin(), str.end(), ::isspace);
}

inline std::string_view ltrim(std::string_view str, std::string_view chars = Blanks) noexcept {
    str.remove_prefix(str.find_first_not_of(chars));
    return str;
}
inline std::string_view rtrim(std::string_view str, std::string_view chars = Blanks) noexcept {
    str.remove_suffix(str.size() - str.find_last_not_of(chars) - 1);
    return str;
}
inline std::string_view trim(std::string_view str, std::string_view chars = Blanks) noexcept {
    str = ltrim(str, chars);
    str = rtrim(str, chars);
    return str;
}

inline std::string toString(std::string_view str) {
    return { str.data(), str.size() };
}
