// Copyright (c) 2005 - 2010
// Seweryn Habdank-Wojewodzki
//
// Distributed under the Boost Software License, Version 1.0.
// ( copy at http://www.boost.org/LICENSE_1_0.txt )

// Trivial Logger
// ==============
// Activate the logger at compilation time by using a flag.
// If flag is not set then logger has to be cleaned up from the code
// or set to the stream which ignores all input;
// Flag has to switch logger style;
// Usage as simple as possible;
// Debugger-like style for debugging purpose;
// Implement some basic configuration procedure.
//
// OTLOG (Standard Output Stream),
// ETLOG (Standard Error  Stream),
// FTLOG (File            Stream),
// else  (Null            Stream),
//
// CLEANTLOG (clean all)

#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _TRI_LOGGER_H_INC_
#define _TRI_LOGGER_H_INC_

#include <ostream>
#include <memory>

#include "Time.h"

//#define CLEANTLOG

//#define OTLOG
//#define ETLOG
//#define FTLOG   ExceptLog

namespace TrivialLogger {

    namespace implementation {
        class TriLoggerImpl;
    }

    // main trivial logger class
    class TriLogger
    {

    private:

        static ::std::unique_ptr<implementation::TriLoggerImpl> _p_tl_impl;

        // Don't forget to declare these functions.
        // Want to make sure they are unaccessable & non-copyable
        // otherwise may accidently get copies of singleton.
        // Don't Implement these functions.
        TriLogger (TriLogger const &);
        template<class T>
        T& operator= (TriLogger const &);

    public:

        TriLogger ();
        ~TriLogger ();

        // check if logger is active
        static bool is_active ();

        // activate and deactivate logger
        static void activate (bool const active);

        // return reference to pointer to output stream
        static ::std::ostream*& ostream_ptr ();

    };

    // important funtion which helps solves
    // "static initialisation fiasco" problem
    // see:
    // 1. S. Habdank-Wojewodzki, "C++ Trivial Logger", Overload 77, Feb 2007, pp.19-23
    // 2. http://www.parashift.com/c++-faq-lite/ctors.html#faq-10.13
    // present solution is much better
    extern TriLogger& instance ();

    namespace implementation
    {
        extern ::std::unique_ptr<TriLogger> p_trilog;
    }
}


#undef TRI_LOG_ON
#undef TRI_LOG_OFF

#undef TRI_LOG_VAR
#undef TRI_LOG_MSG

//#undef TRI_LOG_FN
//#undef TRI_LOG_INFO

#ifdef CLEANTLOG

#define TRI_LOG_ON()        ((void) 0)
#define TRI_LOG_OFF()       ((void) 0)

#define TRI_LOG_VAR(var)    ((void) 0)
#define TRI_LOG_MSG(str)    ((void) 0)

//#define TRI_LOG_FN(var)     ((void) 0)
//#define TRI_LOG_INFO(str)   ((void) 0)

#else

// macros for switching off and on logger
#define TRI_LOG_ON()    TrivialLogger::instance ().activate (true)
#define TRI_LOG_OFF()   TrivialLogger::instance ().activate (false)

// macro prints variable name and its value to the logger stream
#define TRI_LOG_VAR(var)                            \
    do {                                            \
    if (TrivialLogger::instance ().is_active ()) {  \
    *TrivialLogger::instance ().ostream_ptr ()      \
    << "[" << Time::now () << "] "                  \
    << "\""<< __FILE__<<"\" ("<<__LINE__<< ") "     \
    << __FUNCTION__ << " () : "                     \
    << "\'"<<#var<<" = "<<var<<"\'" << std::endl;   \
    } } while (false)

// macro prints value of constant strings to the logger stream
#define TRI_LOG_MSG(msg)                            \
    do {                                            \
    if (TrivialLogger::instance ().is_active ()) {  \
    *TrivialLogger::instance ().ostream_ptr ()      \
    << "[" << Time::now () << "] "                  \
    << "\""<< __FILE__<<"\" ("<<__LINE__<< ") "     \
    << __FUNCTION__ << " () : "                     \
    << "\"" << msg << "\"" << std::endl;            \
    } } while (false)



// namespace for the trivial logger
//namespace TrivialLogger {
//
//    // example how to create functions which operates on logger stream
//    // here are used templates for preparing function which is independent
//    // on the type, but this is not required
//    template <typename T1, typename T2, typename T3, typename T4> 
//    void put_debug_info (TriLogger & log, const T1 & t1, const T2 &t2, const T3 &t3, const T4 &t4)
//    {
//        if (log.is_active())
//        {
//            *(log.ostream_ptr()) << t1 << " (" << t2 << ") : ";
//            *(log.ostream_ptr()) << t3 << " = " << t4 << std::endl;
//        } 
//    }
//    
//    template <typename T> 
//    void put_log_info (TriLogger &log, const T & t)
//    {
//        if (log.is_active())
//        {
//            *(log.ostream_ptr()) << t << std::endl;
//        } 
//    }
//}
//
//// macro shows how to write macros which using user-defined functions
//#define TRI_LOG_FN(var)   ::TrivialLogger::put_debug_info (TrivialLogger::instance (), __FILE__, __LINE__, #var, (var))
//
//// below is a place for user defined logger formating data
//
//// ...
//
//#define TRI_LOG_INFO(var) ::TrivialLogger::put_log_info (TrivialLogger::instance (), (var))
//


#endif

#endif // _TRI_LOGGER_H_INC_
