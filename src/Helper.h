#pragma once

#include <cassert>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "Type.h"

// Return the sign of a number (-1, 0, 1)
template<typename T>
constexpr i32 sign(T const &v) {
    return (T(0) < v) - (v < T(0));
}

template<typename T>
T const& clamp(T const &v, T const &minimum, T const &maximum) {
    return (minimum > v) ? minimum :
           (v > maximum) ? maximum : v;
}

/// std::string Helpers

extern bool whiteSpaces(std::string const&);

extern std::string& toLower(std::string&);
extern std::string& toUpper(std::string&);
extern std::string& toggle(std::string&);
extern std::string& reverse(std::string&);
extern std::string& replace(std::string&, char const, char const);

extern std::string& ltrim(std::string&);
extern std::string& rtrim(std::string&);
extern std::string& trim(std::string&);

extern std::string appendPath(std::string const&, std::string const&);
extern void removeExtension(std::string&);

//extern void eraseSubstring(std::string&, std::string const&);
//extern void eraseSubstrings(std::string&, std::vector<std::string> const&);

//extern std::vector<std::string> splitString(std::string const&, char = ' ', bool = true, bool = false);


enum OutputState : u08 { OS_LOCK, OS_UNLOCK };

extern std::ostream& operator<<(std::ostream&, OutputState);

#define sync_cout std::cout << OS_LOCK
#define sync_endl std::endl << OS_UNLOCK
