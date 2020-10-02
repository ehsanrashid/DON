#pragma once

#include <cstring>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>
#include "string_view.h"

inline std::string toLower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}
inline std::string toUpper(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}

inline std::string toggleCase(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(),
        [](int ch) { return ::islower(ch) ? ::toupper(ch) : ::tolower(ch); });
    return str;
}

inline std::vector<std::string> split(std::string_view str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream iss{ str.data() };
    while (std::getline(iss, token, delimiter)) {

        std::replace(token.begin(), token.end(), '\\', '/');
        //token = toString(trim(token));
        //if (whiteSpaces(token)) {
        //    continue;
        //}
        tokens.push_back(token);
    }
    return tokens;
}


//constexpr std::string_view BlanksS = " \f\n\r\t\v";
//
//inline std::string ltrim(std::string str, std::string_view chars = BlanksS) {
//    /*
//    str.erase(str.begin(), std::find_if(str.begin(), str.end(),
//        [](int ch) {
//            return !(std::isspace(ch) || ch == '\0');
//        }));
//    */
//    str.erase(0, str.find_first_not_of(chars));
//    return str;
//}
//
//inline std::string rtrim(std::string str, std::string_view chars = BlanksS) {
//    /*
//    str.erase(std::find_if(str.rbegin(), str.rend(),
//        [](int ch) {
//            return !(std::isspace(ch) || ch == '\0');
//        }).base(), str.end());
//    */
//    str.erase(str.find_last_not_of(chars) + 1);
//    return str;
//}
//
//inline std::string trim(std::string str, std::string_view chars = BlanksS) {
//    /*
//    auto beg{ str.find_first_not_of(chars) };
//    if (beg != string::npos) {
//        auto end{ str.find_last_not_of(chars) };
//        str = str.substr(beg, (end - beg + 1));
//    }
//    */
//    str = ltrim(str, chars);
//    str = rtrim(str, chars);
//
//    return str;
//}

/*
constexpr char toLower(char c) {
    return (c >= 'A' && c <= 'Z' ? (c - 'A') + 'a' : c);
}
constexpr char toUpper(char c) {
    return (c >= 'a' && c <= 'z' ? (c - 'a') + 'A' : c);
}
inline std::string toLower(std::string const &str) {
    std::string s;
    for (auto ch : str) {
        s += toLower(ch);
    }
    return s;
}
inline std::string toUpper(std::string const &str) {
    std::string s;
    for (auto ch : str) {
        s += toLower(ch);
    }
    return s;
}
*/
