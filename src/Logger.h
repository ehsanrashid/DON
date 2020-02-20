#pragma once

#include <fstream>
#include <iostream>
#include <string>

#include "TieStreamBuf.h"
#include "Type.h"

// Singleton Logger
class Logger
{
private:

    std::ofstream ofs;

    TieStreamBuf iTSB
        ,        oTSB;

protected:

    Logger();
    ~Logger();

public:

    static Logger& instance();

    // Delete copy and move constructors and assign operators
    Logger(const Logger&) = delete;             // Copy construct
    Logger(Logger&&) = delete;                  // Move construct
    Logger& operator=(const Logger&) = delete;  // Copy assign
    Logger& operator=(Logger&&) = delete;      // Move assign

    void setFile(const std::string&);
};
