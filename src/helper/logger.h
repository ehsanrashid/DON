#pragma once

#include <fstream>
#include <iostream>
#include <string>

#include "tiestreambuffer.h"

// Logger class
// Tie std::istream and std::ostream to a file std::ofstream.
class Logger {

public:

    Logger(std::istream &, std::ostream &) noexcept;
    ~Logger();

    // Delete copy and move constructors and assign operators
    Logger(Logger const&) = delete;
    Logger(Logger&&) = delete;

    Logger& operator=(Logger const&) = delete;
    Logger& operator=(Logger&&) = delete;

    void setup(std::string_view);

private:

    std::istream &istream;
    std::ostream &ostream;

    std::string filename;
    std::ofstream ofstream;

    TieStreamBuffer itiestreambuf;
    TieStreamBuffer otiestreambuf;
};

