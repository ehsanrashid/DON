#ifndef _DEBUGGER_H_INC_
#define _DEBUGGER_H_INC_

#include <fstream>

#include "Type.h"
#include "tiebuffer.h"

#if defined(_WIN32)
#   include <ctime>
#endif


inline std::string time_to_string (const std::chrono::system_clock::time_point &tp)
{

#   if defined(_WIN32)

    auto time = std::chrono::system_clock::to_time_t (tp);
    auto tp_sec = std::chrono::system_clock::from_time_t (time);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds> (tp - tp_sec).count ();
    auto *ttm = localtime (&time);
    const char date_time_format[] = "%Y.%m.%d-%H.%M.%S";
    char time_str[] = "yyyy.mm.dd.HH-MM.SS.fff";
    strftime (time_str, strlen (time_str), date_time_format, ttm);
    std::string stime (time_str);
    stime.append (".");
    stime.append (std::to_string (ms));
    return stime;

#   endif

}

template<class CharT, class Traits>
inline std::basic_ostream<CharT, Traits>&
    operator<< (std::basic_ostream<CharT, Traits> &os, const std::chrono::system_clock::time_point &tp)
{
    os << time_to_string (tp);
    return os;
}

namespace Debugger {

    // Singleton I/O Logger class
    class Logger
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
        
        Logger (const Logger&) = delete;
        Logger& operator= (const Logger&) = delete;

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
                _fstm.open ("DebugLog.txt", std::ios_base::out|std::ios_base::app);
                _fstm << "[" << std::chrono::system_clock::now () << "] ->" << std::endl;

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

                _fstm << "[" << std::chrono::system_clock::now () << "] <-" << std::endl;
                _fstm.close ();
            }
        }

    };

    extern void dbg_hits_on (bool h, bool c = true);
    extern void dbg_mean_of (u64 v);
    extern void dbg_print ();

}

#endif // _DEBUGGER_H_INC_
