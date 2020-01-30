#pragma once

#include <fstream>
#include <iostream>
#if defined(_WIN32)
#   include <ctime>
#endif

#include "Engine.h"
#include "tiebuffer.h"
#include "Type.h"
#include "Util.h"


inline std::string time_to_string(const std::chrono::system_clock::time_point &tp)
{
    std::string stime;

#   if defined(_WIN32)

    auto time = std::chrono::system_clock::to_time_t(tp);
    const auto *local_tm = localtime(&time);
    const char *format = "%Y.%m.%d-%H.%M.%S";
    char buffer[32];
    strftime(buffer, sizeof (buffer), format, local_tm);
    stime.append(buffer);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp - std::chrono::system_clock::from_time_t(time)).count();
    stime.append(".");
    stime.append(std::to_string(ms));

#   else

    (void)tp;

#   endif

    return stime;
}

template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
    operator<<(std::basic_ostream<Elem, Traits> &os, const std::chrono::system_clock::time_point &tp)
{
    os << time_to_string(tp);
    return os;
}

// Singleton I/O Logger class
class Logger
{
private:
    std::ofstream _ofs;
    std::tie_buf  _inb; // Input
    std::tie_buf  _otb; // Output

public:

    std::string filename;

    Logger()
        : _inb(std::cin.rdbuf(), _ofs.rdbuf())
        , _otb(std::cout.rdbuf(), _ofs.rdbuf())
        , filename("<empty>")
    {}
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    virtual ~Logger()
    {
        set("<empty>");
    }

    void set(const std::string &fn)
    {
        if (_ofs.is_open())
        {
            std::cout.rdbuf(_otb.streambuf());
            std::cin.rdbuf(_inb.streambuf());

            _ofs << "[" << std::chrono::system_clock::now() << "] <-" << std::endl;
            _ofs.close();
        }
        filename = fn;
        if (!white_spaces(filename))
        {
            _ofs.open(filename, std::ios_base::out|std::ios_base::app);
            if (!_ofs.is_open())
            {
                std::cerr << "Unable to open debug log file " << filename << std::endl;
                stop(EXIT_FAILURE);
            }
            _ofs << "[" << std::chrono::system_clock::now() << "] ->" << std::endl;

            std::cin.rdbuf(&_inb);
            std::cout.rdbuf(&_otb);
        }
    }

};

// Global Logger
extern Logger Log;

// Debug functions used mainly to collect run-time statistics
extern void debug_init();
extern void debug_hit(bool);
extern void debug_hit_on(bool, bool);
extern void debug_mean_of(i64);
extern void debug_print();
