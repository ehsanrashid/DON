#pragma once

#include <fstream>
#include <iostream>
#include <string>

#include "TieStreamBuf.h"
#include "Type.h"

// Logger class (singleton)
// Tie std::cin and std::cout to a out file stream.
class Logger {

private:

    std::string _fnLog;
    std::ofstream _ofs;

    TieStreamBuf
        _tsbInnput,
        _tsbOutput;

protected:

    Logger();
    ~Logger();

public:

    static Logger& instance();

    // Delete copy and move constructors and assign operators
    Logger(Logger const&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger const&) = delete;
    Logger& operator=(Logger&&) = delete;

    void setup(std::string const&);
};
