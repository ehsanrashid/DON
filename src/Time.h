#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _TIME_H_INC_
#define _TIME_H_INC_

#include <iomanip>
#include <sstream>
#include <iostream>

#include "Platform.h"

#include <ctime>


#if defined(_WIN32) || defined(_MSC_VER) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

#   ifdef _MSC_VER
#       pragma warning (disable: 4996) // Function _ftime() may be unsafe
#   endif

#   include <sys/timeb.h>

INLINE uint64_t system_time_msec ()
{
    _timeb timebuf;
    _ftime (&timebuf);
    //_ftime_s (&timebuf);
    return ((timebuf.time * 1000LL) + timebuf.millitm);
}

#else   // LINUX - UNIX

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
    //    M_SEC = 1000,
    //} point;
    //INLINE point  operator-  (const point &p1, const point &p2) { return point (uint64_t (p1) - uint64_t (p2)); }

    typedef int64_t     point;
    const point M_SEC = 1000;

    INLINE point now () { return point (system_time_msec ()); }

    inline std::string to_string (const point &p)
    {
        std::ostringstream ost;

#   if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

        time_t time = (p / M_SEC);
        char *str_time = ctime (&time);

        //char str_time[26];
        //errno_t err = ctime_s (str_time, sizeof (str_time), &time);
        //if (err)

        if (!*str_time)
        {
            ost << "ERROR: Invalid time '" << time << "'";
            return ost.str ();
        }

        str_time[10] = '\0';
        str_time[19] = '\0';
        str_time[24] = '\0';

        ost << std::setfill ('0')
            << &str_time[00] << " "
            << &str_time[20] << " "
            << &str_time[11] << "."
            << std::setw (3) << (p % M_SEC);

#   else

//        // TODO::

#   endif

        return ost.str ();
    }
}

template<typename charT, typename Traits>
inline std::basic_ostream<charT, Traits>&
    operator<< (std::basic_ostream<charT, Traits> &os, const Time::point &p)
{
    os << Time::to_string (p);
    return os;
}

#endif // _TIME_H_INC_
