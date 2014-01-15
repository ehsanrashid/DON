//#pragma once
#ifndef TIME_H_
#define TIME_H_

#include <iomanip>
#include <sstream>
#include <iostream>

#include "Platform.h"

#ifdef _WIN32   // WINDOWS

#   include <sys/timeb.h>
//#   include <time.h>

INLINE uint64_t system_time_msec ()
{
    _timeb timebuf;
    //_ftime (&timebuf);
    _ftime_s (&timebuf);
    return ((timebuf.time * 1000LL) + timebuf.millitm);
}

#else           // LINUX - UNIX

#   include <sys/time.h>

INLINE uint64_t system_time_msec ()
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

    INLINE uint64_t  operator-  (point  p1, point p2) { return (uint64_t (p1) - uint64_t (p2)); }

    INLINE point now () { return point (system_time_msec ()); }

    inline std::string to_string (const point t)
    {
        std::ostringstream stime;

#ifdef _WIN32

        time_t time = (t / Time::ONE_SEC);

        char str_time[26];
        errno_t err = ctime_s (str_time, sizeof (str_time), &time);
        if (err) return std::string ("ERROR: Invalid time ") + std::to_string (time);
        str_time[10] = '\0';
        str_time[19] = '\0';
        str_time[24] = '\0';

        stime << std::setfill ('0')
            << &str_time[0] << " "
            << &str_time[20] << " "
            << &str_time[11] << "."
            << std::setw (3) << (t % Time::ONE_SEC);

#else

        // TODO::

#endif

        return stime.str ();
    }
}

template<typename charT, typename Traits>
inline std::basic_ostream<charT, Traits>&
    operator<< (std::basic_ostream<charT, Traits> &os, const Time::point t)
{
    os << Time::to_string (t);
    return os;
}

#endif