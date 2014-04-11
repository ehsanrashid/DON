#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _TIE_BUFFER_H_INC_
#define _TIE_BUFFER_H_INC_

#include <streambuf>
#include <fstream>
#include <cstring>

#include "Platform.h"
#include "noncopyable.h"

namespace std {

    template<class Elem, class Traits>
    class basic_tie_buf
        : public basic_streambuf<Elem, Traits>
        , public noncopyable
    {

    private:
        basic_streambuf<Elem, Traits> *_strmbuf;
        basic_ofstream <Elem, Traits> *_filestm;

    public:

        typedef typename Traits::int_type   int_type;

        basic_tie_buf (
            basic_streambuf<Elem, Traits> *strmbuf,
            basic_ofstream <Elem, Traits> *filestm)
            : _strmbuf (strmbuf)
            , _filestm (filestm)
        {}

        inline basic_streambuf<Elem, Traits>* sbuf () const
        {
            return _strmbuf;
        }

        inline int_type write (int_type c, const Elem prefix[])
        {
            static int_type last_ch = '\n';
            
            bool error = false;
            if ('\n' == last_ch)
            {
                u32 length = u32 (strlen (prefix));
                if (_filestm->rdbuf ()->sputn (prefix, length) != length)
                {
                    error = true;
                }
            }
            if (error) return EOF;

            last_ch = _filestm->rdbuf ()->sputc (Elem (c));

            return last_ch;
        }

    protected:

        virtual int sync () override
        {
            _filestm->rdbuf ()->pubsync ();
            return _strmbuf->pubsync ();
        }

        virtual int_type overflow (int_type c) override
        {
            return write (_strmbuf->sputc (Elem (c)), "<< ");
        }

        virtual int_type underflow () override
        {
            return _strmbuf->sgetc ();
        }

        virtual int_type uflow () override
        {
            return write (_strmbuf->sbumpc (), ">> ");
        }

    };

    typedef basic_tie_buf<char,    char_traits<char> >     tie_buf;
    typedef basic_tie_buf<wchar_t, char_traits<wchar_t> >  tie_wbuf;
}

#endif // _TIE_BUFFER_H_INC_
