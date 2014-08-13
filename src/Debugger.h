#ifndef _DEBUGGER_H_INC_
#define _DEBUGGER_H_INC_

#include <iostream>

#include "noncopyable.h"
#include "tiebuffer.h"
#include "Time.h"
#include "UCI.h"

namespace Debugger {

    extern void dbg_hits_on (bool h, bool c = true);
    extern void dbg_mean_of (u64 v);
    extern void dbg_print ();

}

class LogFile
    : public std::ofstream
{

public:
    explicit LogFile (const std::string &fn = "Log.txt")
        : std::ofstream (fn.c_str (), std::ios_base::out|std::ios_base::app)
    {}
    
    ~LogFile ()
    {
        if (is_open ()) close ();
    }

};

// Singleton Debug(I/O) Logger class
class DebugLogger
    : public std::noncopyable
{

private:
    std::ofstream _fstm;
    std::tie_buf  _inbuf;
    std::tie_buf  _outbuf;
    std::string   _log_fn;

protected:

    // Constructor should be protected !!!
    DebugLogger ()
        : _inbuf (std::cin .rdbuf (), &_fstm)
        , _outbuf (std::cout.rdbuf (), &_fstm)
    {
        _log_fn = std::string(Options["Debug Log"]);
        if (!_log_fn.empty ())
        {
            trim (_log_fn);
            if (!_log_fn.empty ())
            {
                convert_path (_log_fn);
                remove_extension (_log_fn);
                if (!_log_fn.empty ()) _log_fn += ".txt";
            }
        }
        if (_log_fn.empty ()) _log_fn = "DebugLog.txt";
    }

public:

    ~DebugLogger ()
    {
        stop ();
    }

    static DebugLogger& instance ()
    {
        // Guaranteed to be destroyed.
        // Instantiated on first use.
        static DebugLogger _instance;

        return _instance;
    }

    void start ()
    {
        if (!_fstm.is_open ())
        {
            _fstm.open (_log_fn.c_str (), std::ios_base::out|std::ios_base::app);
            _fstm << "[" << Time::to_string (Time::now ()) << "] ->" << std::endl;

            std::cin .rdbuf (&_inbuf);
            std::cout.rdbuf (&_outbuf);
        }
    }

    void stop ()
    {
        if (_fstm.is_open ())
        {
            std::cout.rdbuf (_outbuf.sbuf ());
            std::cin .rdbuf (_inbuf.sbuf ());

            _fstm << "[" << Time::to_string (Time::now ()) << "] <-" << std::endl;
            _fstm.close ();
        }
    }

};

inline void log_debug (bool b)
{
    (b) ? DebugLogger::instance ().start ()
        : DebugLogger::instance ().stop ();
}

#endif // _DEBUGGER_H_INC_
