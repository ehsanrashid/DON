#ifndef _TIME_H_INC_
#define _TIME_H_INC_

#include <iomanip>
#include <sstream>
#include <iostream>
#include <ctime>
#include <chrono>

#include "Platform.h"

#if defined(_WIN32)

#   ifdef _MSC_VER
#       pragma warning (disable: 4996) // Function _ftime() may be unsafe
#   endif

#   include <sys/timeb.h>

inline u64 system_time_msec ()
{
    _timeb timebuf;
    _ftime (&timebuf);
    return ((timebuf.time * 1000LL) + timebuf.millitm);
}

#else   // LINUX - UNIX

#   include <sys/time.h>

inline u64 system_time_msec ()
{
    timeval timebuf;
    gettimeofday (&timebuf, NULL);
    return ((timebuf.tv_sec * 1000LL) + (timebuf.tv_usec / 1000));
}

#endif

typedef std::chrono::milliseconds::rep TimePoint; // A value in milliseconds

const TimePoint MILLI_SEC        = 1000;
const TimePoint MINUTE_MILLI_SEC = MILLI_SEC * 60;
const TimePoint HOUR_MILLI_SEC   = MINUTE_MILLI_SEC * 60;

inline TimePoint now()
{
    return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch ()).count ();
}

inline std::string time_to_string (const TimePoint &p)
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

template<class CharT, class Traits>
inline std::basic_ostream<CharT, Traits>&
    operator<< (std::basic_ostream<CharT, Traits> &os, TimePoint p)
{
    os << time_to_string (p);
    return os;
}

#endif // _TIME_H_INC_
