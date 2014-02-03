//#pragma once
#ifndef LOG_H_
#define LOG_H_

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

#endif