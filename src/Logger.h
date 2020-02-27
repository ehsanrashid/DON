#pragma once

#include <fstream>
#include <iostream>
#include <string>

#include "TieStreamBuf.h"
#include "Type.h"

// Singleton Logger
// Tie std::cin and std::cout to a out file stream.
class Logger {

private:

    std::string _fnLog;
    std::ofstream _ofs;

    TieStreamBuf _tsbInnput
        ,        _tsbOutput;

protected:

    Logger();
    ~Logger();

public:

    static Logger& instance();

    // Delete copy and move constructors and assign operators
    Logger(Logger const&) = delete;             // Copy construct
    Logger(Logger&&) = delete;                  // Move construct
    Logger& operator=(Logger const&) = delete;  // Copy assign
    Logger& operator=(Logger&&) = delete;       // Move assign

    void setFile(std::string const&);
};
