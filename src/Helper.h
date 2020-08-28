#pragma once

#include <cassert>
#include <functional>
#include <sstream>
#include <string>

#include "Type.h"

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
