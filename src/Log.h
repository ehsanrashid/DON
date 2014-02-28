#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _LOG_H_INC_
#define _LOG_H_INC_

#include <fstream>

typedef class Log
    : public std::ofstream
{

public:
    Log(const std::string& fn = "log.txt")
        : std::ofstream(fn, std::ios_base::out | std::ios_base::app)
    {}
    
    ~Log()
    {
        if (is_open ()) close ();
    }

} Log;

#endif // _LOG_H_INC_
