#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _DEBUG_LOGGER_H_INC_
#define _DEBUG_LOGGER_H_INC_

#include <iostream>
#include "noncopyable.h"
#include "tiebuffer.h"
#include "Time.h"

typedef class LogFile
    : public std::ofstream
{

public:
    LogFile(const std::string& fn = "Log.txt")
        : std::ofstream(fn, std::ios_base::out|std::ios_base::app)
    {}
    
    ~LogFile()
    {
        if (is_open ()) close ();
    }

} LogFile;

// Singleton I/O logger class
typedef class DebugLogger : std::noncopyable
{

private:
    std::ofstream _fstm;
    std::tie_buf  _inbuf;
    std::tie_buf  _outbuf;
    std::string   _log_fn;

protected:

    // Constructor should be protected !!!
    DebugLogger (std::string log_fn)
        :  _inbuf (std::cin .rdbuf (), &_fstm)
        , _outbuf (std::cout.rdbuf (), &_fstm)
        , _log_fn (log_fn)
    {}

public:

    ~DebugLogger ()
    {
        stop ();
    }

    static DebugLogger& instance ()
    {
        // Guaranteed to be destroyed.
        // Instantiated on first use.
        static DebugLogger _instance ("DebugLog.txt");
        return _instance;
    }

    void start ()
    {
        if (!_fstm.is_open ())
        {
            _fstm.open (_log_fn, std::ios_base::out|std::ios_base::app);
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

} DebugLogger;

inline void log_debug (bool b)
{
    (b) ? DebugLogger::instance ().start ()
        : DebugLogger::instance ().stop ();
}


extern void dbg_hit_on (bool h, bool c = true);
extern void dbg_mean_of (uint64_t v);
extern void dbg_print ();

#endif // _DEBUG_LOGGER_H_INC_
