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
    virtual ~Logger();

public:

    static Logger& instance();

    // Delete copy and move constructors and assign operators
    Logger(const Logger&) = delete;             // Copy construct
    Logger(Logger&&) = delete;                  // Move construct
    Logger& operator=(const Logger&) = delete;  // Copy assign
    Logger& operator=(Logger&&) = delete;      // Move assign

    void set(const std::string&);
};


using SystemClockTimePoint = std::chrono::system_clock::time_point;

extern std::string toString(const SystemClockTimePoint&);
extern std::ostream& operator<<(std::ostream&, const SystemClockTimePoint&);
