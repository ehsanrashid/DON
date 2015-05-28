#ifndef _DEBUGGER_H_INC_
#define _DEBUGGER_H_INC_

#include <fstream>

#include "noncopyable.h"
#include "tiebuffer.h"
#include "Time.h"
#include "UCI.h"

namespace Debugger {

    extern void dbg_hits_on (bool h, bool c = true);
    extern void dbg_mean_of (u64 v);
    extern void dbg_print ();

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

    // Singleton Debug(I/O) Logger class
    class DebugLogger
        : public std::noncopyable
    {

    private:
        std::ofstream _fstm;
        std::tie_buf  _innbuf;
        std::tie_buf  _outbuf;
        std::string   _log_fn;

    protected:

        // Constructor should be protected !!!
        DebugLogger ()
            : _innbuf (std::cin .rdbuf (), &_fstm)
            , _outbuf (std::cout.rdbuf (), &_fstm)
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
            static DebugLogger _instance;

            return _instance;
        }

        void start ()
        {
            if (!_fstm.is_open ())
            {
                _log_fn = std::string(Options["Debug Log"]);
                if (!white_spaces (_log_fn))
                {
                    trim (_log_fn);
                    if (!white_spaces (_log_fn))
                    {
                        convert_path (_log_fn);
                        remove_extension (_log_fn);
                        if (!white_spaces (_log_fn)) _log_fn += ".txt";
                    }
                }
                if (white_spaces (_log_fn)) _log_fn = "DebugLog.txt";
            
                _fstm.open (_log_fn, std::ios_base::out|std::ios_base::app);
                _fstm << "[" << time_to_string (now ()) << "] ->" << std::endl;

                std::cin .rdbuf (&_innbuf);
                std::cout.rdbuf (&_outbuf);
            }
        }

        void stop ()
        {
            if (_fstm.is_open ())
            {
                std::cout.rdbuf (_outbuf.sbuf ());
                std::cin .rdbuf (_innbuf.sbuf ());

                _fstm << "[" << time_to_string (now ()) << "] <-" << std::endl;
                _fstm.close ();
            }
        }

    };

    inline void log_debug (bool b)
    {
        (b) ? DebugLogger::instance ().start ()
            : DebugLogger::instance ().stop ();
    }

}

#endif // _DEBUGGER_H_INC_
