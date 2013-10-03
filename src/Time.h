//#pragma once
#ifndef TIME_H_
#define TIME_H_

#include <iosfwd>
#include <string>
#include "Platform.h"


#if defined(_WIN32) // WINDOWS

#   include <sys/timeb.h>
#   include <time.h>

inline uint64_t system_time_msec ()
{
    _timeb timebuf;
    //_ftime (&timebuf);
    _ftime_s (&timebuf);
    return ((timebuf.time * 1000LL) + timebuf.millitm);
}

#else               // LINUX - UNIX

#   include <sys/time.h>

inline uint64_t system_time_msec ()
{
    timeval timebuf;
    gettimeofday (&timebuf, NULL);
    return ((timebuf.tv_sec * 1000LL) + (timebuf.tv_usec / 1000));
}

#endif

namespace Time {

    typedef enum point : uint64_t
    {
        ONE_SEC = 1000,
    } point;

    inline point now () { return point (system_time_msec ()); }

    ::std::string to_string (const point point);

}

template<typename charT, typename Traits>
extern ::std::basic_ostream<charT, Traits>& operator<< (
    ::std::basic_ostream<charT, Traits>& ostream,
    const Time::point point);


#endif