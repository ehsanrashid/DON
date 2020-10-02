#pragma once

#include <string>

namespace CommandLine {

    extern std::string binaryDirectory;  // path of the executable directory
    extern std::string workingDirectory; // path of the working directory

    extern void initialize(int, char const *const[]);
}
