#pragma once

#include <cassert>
#include <functional>
#include <sstream>
#include <string>
#include <streambuf>

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

namespace CommandLine {

    void initialize(int, char const *const*);

    extern std::string binaryDirectory;  // path of the executable directory
    extern std::string workingDirectory; // path of the working directory
}

// C++ way to prepare a buffer for a memory stream
class MemoryBuffer :
    public std::basic_streambuf<char> {

public:

    MemoryBuffer(char *p, size_t n) {
        setg(p, p, p + n);
        setp(p, p + n);
    }
};

