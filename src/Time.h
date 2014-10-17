#ifndef _TIME_H_INC_
#define _TIME_H_INC_

#include <iomanip>
#include <sstream>
#include <iostream>
#include <ctime>

#include "Platform.h"

#if defined(_WIN32)

#   ifdef _MSC_VER
#       pragma warning (disable: 4996) // Function _ftime() may be unsafe
#   endif

#   include <sys/timeb.h>

INLINE u64 system_time_msec ()
{
    _timeb timebuf;
    _ftime (&timebuf);
    return ((timebuf.time * 1000LL) + timebuf.millitm);
}

#else   // LINUX - UNIX

#   include <sys/time.h>

INLINE u64 system_time_msec ()
{
    timeval timebuf;
    gettimeofday (&timebuf, NULL);
    return ((timebuf.tv_sec * 1000LL) + (timebuf.tv_usec / 1000));
}

#endif

namespace Time {

    typedef i64     point;

    const point MILLI_SEC        = 1000;
    const point MINUTE_MILLI_SEC = MILLI_SEC * 60;
    const point HOUR_MILLI_SEC   = MINUTE_MILLI_SEC * 60;

    INLINE point now () { return system_time_msec (); }

    inline std::string to_string (const point &p)
    {
        std::ostringstream oss;

#   if defined(_WIN32)

        time_t time = (p / MILLI_SEC);
        char *str_time = ctime (&time);

        //char str_time[26];
        //errno_t err = ctime_s (str_time, sizeof (str_time), &time);
        //if (err)

        if (!str_time[00])
        {
            oss << "ERROR: invalid time '" << time << "'";
            return oss.str ();
        }

        str_time[10] = '\0';
        str_time[19] = '\0';
        str_time[24] = '\0';

        oss << std::setfill ('0')
            << &str_time[00] << " "
            << &str_time[20] << " "
            << &str_time[11] << "."
            << std::setw (3) << (p % MILLI_SEC);

#   else

#   endif

        return oss.str ();
    }
}

template<class CharT, class Traits>
inline std::basic_ostream<CharT, Traits>&
    operator<< (std::basic_ostream<CharT, Traits> &os, Time::point p)
{
    os << Time::to_string (p);
    return os;
}

#endif // _TIME_H_INC_
