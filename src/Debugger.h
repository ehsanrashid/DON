#ifndef _DEBUGGER_H_INC_
#define _DEBUGGER_H_INC_

#include <fstream>

#include "Type.h"
#include "tiebuffer.h"
#include "noncopyable.h"

#if defined(_WIN32)
#   include <time.h>
#endif


inline std::string time_to_string (const TimePoint &/*p*/)
{
    std::ostringstream oss;
/*
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
*/
    return oss.str ();
}

template<class CharT, class Traits>
inline std::basic_ostream<CharT, Traits>&
    operator<< (std::basic_ostream<CharT, Traits> &os, TimePoint p)
{
    os << time_to_string (p);
    return os;
}

namespace Debugger {

    class LogFile
        : public std::ofstream
    {

    public:
        explicit LogFile (const std::string &fn = "Log.txt")
            : std::ofstream (fn, out|app)
        {}

        ~LogFile ()
        {
            if (is_open ()) close ();
        }

    };

    // Singleton I/O Logger class
    class Logger
        : public std::noncopyable
    {

    private:
        std::ofstream _fstm;
        std::tie_buf  _innbuf;
        std::tie_buf  _outbuf;

    protected:

        // Constructor should be protected !!!
        Logger ()
            : _innbuf (std::cin .rdbuf (), _fstm.rdbuf ())
            , _outbuf (std::cout.rdbuf (), _fstm.rdbuf ())
        {}

    public:

        ~Logger ()
        {
            stop ();
        }

        static Logger& instance ()
        {
            // Guaranteed to be destroyed.
            // Instantiated on first use.
            static Logger _instance;

            return _instance;
        }

        void start ()
        {
            if (!_fstm.is_open ())
            {
                _fstm.open ("io_log.txt", std::ios_base::out|std::ios_base::app);
                _fstm << "[" << time_to_string (now ()) << "] ->" << std::endl;

                std::cin .rdbuf (&_innbuf);
                std::cout.rdbuf (&_outbuf);
            }
        }

        void stop ()
        {
            if (_fstm.is_open ())
            {
                std::cout.rdbuf (_outbuf.streambuf ());
                std::cin .rdbuf (_innbuf.streambuf ());

                _fstm << "[" << time_to_string (now ()) << "] <-" << std::endl;
                _fstm.close ();
            }
        }

    };

    extern void dbg_hits_on (bool h, bool c = true);
    extern void dbg_mean_of (u64 v);
    extern void dbg_print ();

}

#endif // _DEBUGGER_H_INC_
