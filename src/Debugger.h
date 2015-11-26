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

#   else
    
    return "";

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
        std::ofstream _ofs;
        std::tie_buf  _inb; // Input
        std::tie_buf  _otb; // Output

    protected:

        // Constructor should be protected !!!
        Logger ()
            : _inb (std::cin .rdbuf (), _ofs.rdbuf ())
            , _otb (std::cout.rdbuf (), _ofs.rdbuf ())
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
            if (!_ofs.is_open ())
            {
                _ofs.open ("DebugLog.txt", std::ios_base::out|std::ios_base::app);
                _ofs << "[" << std::chrono::system_clock::now () << "] ->" << std::endl;

                std::cin .rdbuf (&_inb);
                std::cout.rdbuf (&_otb);
            }
        }

        void stop ()
        {
            if (_ofs.is_open ())
            {
                std::cout.rdbuf (_otb.streambuf ());
                std::cin .rdbuf (_inb.streambuf ());

                _ofs << "[" << std::chrono::system_clock::now () << "] <-" << std::endl;
                _ofs.close ();
            }
        }

    };

    // Debug functions used mainly to collect run-time statistics
    extern void dbg_hit_on (bool hit);
    extern void dbg_hit_on (bool cond, bool hit);

    extern void dbg_mean_of (i64 item);
    
    extern void dbg_print ();

}

#endif // _DEBUGGER_H_INC_
