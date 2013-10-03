#include "Time.h"

#include <iomanip>
#include <sstream>
#include <iostream>

namespace Time {

    std::string to_string (const point point)
    {

#if defined(_WIN32)

        time_t time = (point / Time::point::ONE_SEC);

        char str_time[26];
        errno_t err = ctime_s (str_time, sizeof (str_time), &time);
        if (err) return std::string ("ERROR: Invalid time ") + ::std::to_string (time);
        
        std::ostringstream stime;

        str_time[10] = '\0';
        str_time[19] = '\0';
        str_time[24] = '\0';

        stime << std::setfill ('0')
            << &str_time[0] << " "
            << &str_time[20] << " "
            << &str_time[11] << "."
            << std::setw (3) << (point % Time::point::ONE_SEC);

#else

        // TODO::

#endif

        return stime.str ();
    }

}

template<typename charT, typename Traits>
std::basic_ostream<charT, Traits>& operator<< (
    std::basic_ostream<charT, Traits>& ostream,
    const Time::point point)
{
    ostream << Time::to_string (point);
    return ostream;
}


template std::basic_ostream<char, std::char_traits<char> >&
    operator<< <char, std::char_traits<char> >(
    std::basic_ostream<char, std::char_traits<char> > &,
    const Time::point point);

//template std::basic_ofstream<char, std::char_traits<char> >&
//    operator<< <char, std::char_traits<char> >(
//    std::basic_ofstream<char, std::char_traits<char> > &,
//    const Time::point point);
