// Copyright (c) 2005 - 2010
// Seweryn Habdank-Wojewodzki
//
// Distributed under the Boost Software License, Version 1.0.
// (copy at http://www.boost.org/LICENSE_1_0.txt)

#include "TriLogger.h"

#ifndef CLEANTLOG

#   if defined(FTLOG)

#       include <fstream>

#   else

#       include <iostream>
#       include "nullstream.h"

#   endif

namespace TrivialLogger {

    using namespace std;

    namespace implementation {

        class TriLoggerImpl
        {

        public:
            // true if logger is active
            static bool _is_active;

            // auto pointer helps manage resources;
            static unique_ptr<ostream> _outstream_ptr;

            // pointer to the output stream of the logger
            static ostream *_outstream;

        };

        // activate logger by default
        bool TriLoggerImpl::_is_active = true;

        void init_tri_logger_impl ();
    }


    unique_ptr<implementation::TriLoggerImpl> 
        TriLogger::_p_tl_impl (unique_ptr<implementation::TriLoggerImpl> (new implementation::TriLoggerImpl ()));


    TriLogger::TriLogger ()
    {
        if (!_p_tl_impl.get ())
        {
            TriLogger::_p_tl_impl.reset (new implementation::TriLoggerImpl ());
        }
        implementation::init_tri_logger_impl ();
    }

    TriLogger::~TriLogger ()
    {}

    bool TriLogger::is_active ()
    {
        return _p_tl_impl->_is_active;
    }

    void TriLogger::activate (bool active)
    {
        _p_tl_impl->_is_active = active;
    }

    ostream*& TriLogger::ostream_ptr ()
    {
        return _p_tl_impl->_outstream;
    }

#   if defined(OTLOG)

    // set auto pointer to the null stream
    // reason: cout can not be created in runtime, so
    // the auto pointer has nothing to do with its resources
    unique_ptr<ostream> implementation::TriLoggerImpl::_outstream_ptr =
        unique_ptr<ostream> (new null_stream ());
    ostream *implementation::TriLoggerImpl::_outstream = &cout;

    void implementation::init_tri_logger_impl ()
    { 
        if (!implementation::TriLoggerImpl::_outstream_ptr.get ())
        {
            implementation::TriLoggerImpl::_outstream_ptr.reset (new null_stream ());
        }
        implementation::TriLoggerImpl::_outstream = &cout;
    }

#   elif defined(ETLOG)

    // set auto pointer to the null stream
    // reason: cerr can not be created in runtime, so
    // the auto pointer has nothing to do with its resources
    unique_ptr<ostream> implementation::TriLoggerImpl::_outstream_ptr =
        unique_ptr<ostream> (new null_stream ());
    ostream *implementation::TriLoggerImpl::_outstream = &cerr;

    void implementation::init_tri_logger_impl ()
    { 
        if (!implementation::TriLoggerImpl::_outstream_ptr.get ())
        {
            implementation::TriLoggerImpl::_outstream_ptr.reset (new null_stream ());
        }
        implementation::TriLoggerImpl::_outstream = &cerr;
    }


#   elif defined(FTLOG)

#       include <cctype>

#       define XSTR(s) STR(s)
#       define STR(s) #s
#       define MIN(x1, x2) ((x1) < (x2) ? (x1) : (x2))
#       define MAX(x1, x2) ((x1) > (x2) ? (x1) : (x2))

    typedef char char_type;

    namespace implementation {

        // Function calculates length of C string
        // It can be used with wide characters
        template < typename Char_type >
        u32 const str_len (const Char_type s[])
        {
            u32 length = 0;
            while (*s)
            {
                ++s;
                ++length;
            }
            return length;
        }

        // Function paste rhs C string to the lhs C string.
        // lhs should be long enough for that operation.
        // Additionally coping is stared from the point which
        // points lhs.
        template < typename Char_type >
        u32 const str_cat (Char_type *&lhs, const Char_type *rhs)
        {
            u32 length = 0;
            while (*rhs)
            {
                *lhs = *rhs;
                ++rhs;
                ++lhs;
                ++length;
            }
            return length;
        }

        // Function copy rhs C string in to the lhs.
        // It do not check size of target C string
        // It starts to copy from the beginning of the C string,
        // but it begins put characters at the point where lhs points,
        // so there can be a problem when lhs points on the end of lhs
        // C string.
        template < typename Char_type >
        u32 const str_cpy (Char_type *&lhs, const Char_type *rhs)
        {
            u32 length = 0;
            while (*rhs)
            {
                *lhs = *rhs;
                ++rhs;
                ++lhs;
                ++length;
            }
            *lhs = '\0';
            return length + 1;
        }

        // Function converts existing file name to the file name
        // which has no non-printable signs and 
        // at the end is added extension.
        // The space sign in file name is converted to the underscore.
        // Lengths of C strings has to be proper.
        template<typename Char_type>
        const u32
            create_filename (
            Char_type filename[],
            const Char_type log_fn[],
            const Char_type log_ext[],
            const Char_type def_fn[])
        {
            u32 length = 0; 

            if (str_len (log_fn) > 1)
            {
                while (*log_fn)
                {
                    // check if characters have grapnical
                    // reprasentation
                    if (0 != isgraph (u08 (*log_fn)))
                    {
                        *filename = *log_fn;
                        ++filename;
                        ++length;
                    }
                    else
                    {
                        // convert space to underscore
                        if (' ' == *log_fn)
                        {
                            *filename = '_';
                            ++filename;
                            ++length;
                        }
                    }
                    ++log_fn;
                }
            }
            else
            {
                //filename = &filename[0];
                length   = str_cpy (filename, def_fn);
            }

            // add extension
            str_cat (filename, log_ext);
            *filename = '\0';

            return length;
        }

        //template<typename T>
        //T const min (T const x1, T const x2) { return (x1 < x2 ? x1 : x2); }

        //template<typename T>
        //T const max (T const x1, T const x2) { return (x1 > x2 ? x1 : x2); }

        const char_type* get_log_fn () { return XSTR (FTLOG); }
        const char_type* get_def_fn () { return "ExceptLog"; }
        // extension C string
        const char_type* get_log_ext() { return ".txt"; }

    }

    // convert definition of the FTLOG to the C string
    const char_type *Log_fn  = implementation::get_log_fn ();
    const char_type *Def_fn  = implementation::get_def_fn ();
    const char_type *Log_ext = implementation::get_log_ext ();

    // container for final file name
    char_type Filename[(MAX (sizeof (Log_fn), sizeof(Def_fn)) + sizeof (Log_ext)) / sizeof(char_type)];
    // create file name
    u32 const Length = implementation::create_filename (Filename, implementation::get_log_fn(), implementation::get_log_ext(), implementation::get_def_fn());

#       undef STR
#       undef XSTR
#       undef MIN
#       undef MAX

    // new file is opened and its destruction is managed by unique_ptr
    unique_ptr<ostream> implementation::TriLoggerImpl::_outstream_ptr =
        unique_ptr<ostream> (new ofstream (Filename, ios_base::out|ios_base::app));
    // set pointer output stream
    ostream *implementation::TriLoggerImpl::_outstream = _outstream_ptr.get ();

    void implementation::init_tri_logger_impl ()
    { 
        if (!implementation::TriLoggerImpl::_outstream_ptr.get ())
        {
            implementation::TriLoggerImpl::_outstream_ptr.reset (new ofstream (Filename, ios_base::out|ios_base::app));
            // set pointer output stream
            implementation::TriLoggerImpl::_outstream =
                implementation::TriLoggerImpl::_outstream_ptr.get ();
        }
    }

    // here is a place for user defined output stream and flag

    //

#   else

    unique_ptr<ostream> implementation::TriLoggerImpl::_outstream_ptr =
        unique_ptr<ostream> (new null_stream ());
    ostream *implementation::TriLoggerImpl::_outstream =
        implementation::TriLoggerImpl::_outstream_ptr.get ();

    void implementation::init_tri_logger_impl ()
    {
        if (!implementation::TriLoggerImpl::_outstream_ptr.get ())
        {
            implementation::TriLoggerImpl::_outstream_ptr.reset (new null_stream ());
            implementation::TriLoggerImpl::_outstream =
                implementation::TriLoggerImpl::_outstream_ptr.get ();
        }
    }

#   endif

    unique_ptr<TriLogger> implementation::p_trilog (new TriLogger ());

    TriLogger& instance ()
    {
        if (!implementation::p_trilog.get ())
        {
            implementation::p_trilog.reset (new TriLogger ());
        }
        return *(implementation::p_trilog);
    }

}

#endif // !CLEANTLOG
