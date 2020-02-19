#pragma once

#include <cassert>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "Type.h"

// Return the sign of a number (-1, 0, 1)
template<typename T>
constexpr i32 sign(const T val) {
    return (T(0) < val) - (val < T(0));
}

template<typename T>
const T& clamp(const T &v, const T &minimum, const T &maximum) {
    return (minimum > v) ? minimum :
           (v > maximum) ? maximum : v;
}

/// std::string Helpers

extern bool whiteSpaces(const std::string&);

extern std::string& toLower(std::string&);
extern std::string& toUpper(std::string&);
extern std::string& toggleCase(std::string&);

extern std::string trim(std::string&);

extern std::string appendPath(const std::string&, const std::string&);
extern void removeExtension(std::string&);

// extern void eraseSubstring(std::string&, const std::string&);
// extern void eraseSubstrings(std::string&, const std::vector<std::string>&);

// extern std::vector<std::string> splitString(const std::string&, char = ' ', bool = true, bool = false);

///

enum OutputState : u08 { OS_LOCK, OS_UNLOCK };

extern std::ostream& operator<<(std::ostream&, OutputState);

#define sync_cout std::cout << OS_LOCK
#define sync_endl std::endl << OS_UNLOCK
