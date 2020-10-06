#pragma once

#include <fstream>
#include <iostream>
#include <string>

#include "tiestreambuffer.h"

// Logger class (singleton)
// Tie std::cin and std::cout to a out file stream.
class Logger {

public:

    static Logger& instance() noexcept;

    // Delete copy and move constructors and assign operators
    Logger(Logger const&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger const&) = delete;
    Logger& operator=(Logger&&) = delete;

    void setup(std::string_view);

protected:

    Logger() noexcept;
    ~Logger();

    static Logger logger;

private:

    std::string filename;
    std::ofstream ofstream;

    TieStreamBuffer itiestreambuf;
    TieStreamBuffer otiestreambuf;
};
