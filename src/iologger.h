//#pragma once
#ifndef IO_LOGGER_H_
#define IO_LOGGER_H_

#include <iostream>
#include "tiebuffer.h"
#include "Time.h"

namespace std {

    // Singleton I/O logger class
    typedef class io_logger sealed
    {

    private:
        std::ofstream _fstm;
        std::tie_buf  _inbuf;
        std::tie_buf  _outbuf;

        // Constructor should be private !!!
        io_logger ()
            : _inbuf (std::cin.rdbuf (), &_fstm)
            , _outbuf (std::cout.rdbuf (), &_fstm)
        {}

        // Don't forget to declare these functions.
        // Want to make sure they are unaccessable & non-copyable
        // otherwise may accidently get copies of singleton.
        // Don't Implement these functions.
        io_logger (io_logger const &);
        template<class T>
        T& operator= (io_logger const &);

    public:
        static std::string fn_log;

        ~io_logger ()
        {
            stop ();
        }

        static io_logger& instance ()
        {
            // Guaranteed to be destroyed.
            // Instantiated on first use.
            static io_logger _instance;
            return _instance;
        }

        void start ()
        {
            if (!_fstm.is_open ())
            {
                _fstm.open (fn_log, std::ios_base::out | std::ios_base::app);
                _fstm << "[" << Time::to_string (Time::now ()) << "] ->" << std::endl;

                std::cin.rdbuf (&_inbuf);
                std::cout.rdbuf (&_outbuf);
            }
        }

        void stop ()
        {
            if (_fstm.is_open ())
            {
                std::cout.rdbuf (_outbuf.sbuf ());
                std::cin.rdbuf (_inbuf.sbuf ());

                _fstm << "[" << Time::to_string (Time::now ()) << "] <-" << std::endl;
                _fstm.close ();
            }
        }

    } io_logger;

    std::string io_logger::fn_log = "log_io.txt";

}

inline void log_io (bool on)
{
    on ?
        std::io_logger::instance ().start () :
        std::io_logger::instance ().stop () ;
}

#endif
