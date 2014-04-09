#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _DEBUGGER_H_INC_
#define _DEBUGGER_H_INC_

#include <iostream>

#include "noncopyable.h"
#include "tiebuffer.h"
#include "Time.h"

class LogFile
    : public std::ofstream
{

public:
    LogFile (const std::string& fn = "Log.txt")
        : std::ofstream (fn.c_str (), std::ios_base::out|std::ios_base::app)
    {}
    
    ~LogFile ()
    {
        if (is_open ()) close ();
    }

};

// Singleton I/O logger class
class IOLogger : std::noncopyable
{

private:
    std::ofstream _fstm;
    std::tie_buf  _inbuf;
    std::tie_buf  _outbuf;
    std::string   _log_fn;

protected:

    // Constructor should be protected !!!
    IOLogger (std::string log_fn)
        :  _inbuf (std::cin .rdbuf (), &_fstm)
        , _outbuf (std::cout.rdbuf (), &_fstm)
        , _log_fn (log_fn)
    {}

public:

    ~IOLogger ()
    {
        stop ();
    }

    static IOLogger& instance ()
    {
        // Guaranteed to be destroyed.
        // Instantiated on first use.
        static IOLogger _instance ("IO_Log.txt");
        return _instance;
    }

    void start ()
    {
        if (!_fstm.is_open ())
        {
            _fstm.open (_log_fn.c_str (), std::ios_base::out|std::ios_base::app);
            _fstm << "[" << Time::to_string (Time::now ()) << "] ->" << std::endl;

            std::cin .rdbuf (& _inbuf);
            std::cout.rdbuf (&_outbuf);
        }
    }

    void stop ()
    {
        if (_fstm.is_open ())
        {
            std::cout.rdbuf (_outbuf.sbuf ());
            std::cin .rdbuf ( _inbuf.sbuf ());

            _fstm << "[" << Time::to_string (Time::now ()) << "] <-" << std::endl;
            _fstm.close ();
        }
    }

};

inline void log_io (bool b)
{
    (b) ? IOLogger::instance ().start ()
        : IOLogger::instance ().stop ();
}

namespace Debugger {

    extern void dbg_hits_on (bool h, bool c = true);
    extern void dbg_mean_of (u64 v);
    extern void dbg_print ();

}

#endif // _DEBUGGER_H_INC_
