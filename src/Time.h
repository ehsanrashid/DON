//#pragma once
#ifndef TIME_H_
#define TIME_H_

#include <iomanip>
#include <sstream>
#include <iostream>

#include "Platform.h"

#include <ctime>

#ifdef _WIN32   // WINDOWS

#   include <sys/timeb.h>

INLINE uint64_t system_time_msec ()
{
    _timeb timebuf;
    _ftime (&timebuf);
    //_ftime_s (&timebuf);
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

    //typedef enum point : uint64_t
    //{
    //    MS_SEC = 1000,
    //} point;
    //INLINE point  operator-  (const point &p1, const point &p2) { return point (uint64_t (p1) - uint64_t (p2)); }

    typedef int64_t point;
    const point MS_SEC = 1000;

    INLINE point now () { return point (system_time_msec ()); }

    inline std::string to_string (const point &p)
    {
        std::ostringstream stime;

//#ifdef _WIN32

        time_t time = (p / MS_SEC);
        char *str_time = ctime (&time);

        //char str_time[26];
        //errno_t err = ctime_s (str_time, sizeof (str_time), &time);
        //if (err)
        //if (!str_time[0])
        //{
        //    return std::string ("ERROR: Invalid time ") + std::to_string (uint64_t (time));
        //}

        str_time[10] = '\0';
        str_time[19] = '\0';
        str_time[24] = '\0';

        stime << std::setfill ('0')
            << &str_time[00] << " "
            << &str_time[20] << " "
            << &str_time[11] << "."
            << std::setw (3) << (p % MS_SEC);

//#else
//
//        // TODO::
//
//#endif

        return stime.str ();
    }
}

template<typename charT, typename Traits>
inline std::basic_ostream<charT, Traits>&
    operator<< (std::basic_ostream<charT, Traits> &os, const Time::point &p)
{
    os << Time::to_string (p);
    return os;
}

#endif
