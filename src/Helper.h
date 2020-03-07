#pragma once

#include <cassert>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

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

extern std::string appendPath(std::string const&, std::string const&);
extern void removeExtension(std::string&);

//extern void eraseSubstring(std::string&, std::string const&);
//extern void eraseSubstrings(std::string&, std::vector<std::string> const&);

//extern std::vector<std::string> splitString(std::string const&, char = ' ', bool = true, bool = false);


enum OutputState : u08 { OS_LOCK, OS_UNLOCK };

extern std::ostream& operator<<(std::ostream&, OutputState);

#define sync_cout std::cout << OS_LOCK
#define sync_endl std::endl << OS_UNLOCK
