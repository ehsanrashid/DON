//#pragma once
#ifndef TIE_BUFFER_H_
#define TIE_BUFFER_H_

#include <streambuf>
#include <fstream>
#include "noncopyable.h"

namespace std {

    template<class Elem, class Traits>
    class basic_tie_buf sealed
        : public ::std::basic_streambuf<Elem, Traits>
        , public ::std::noncopyable
    {

    private:
        ::std::basic_streambuf<Elem, Traits> *_sbuf;
        ::std::basic_ofstream <Elem, Traits> *_fstm;

    public:

        //typedef typename ::std::basic_streambuf<Elem, Traits>::int_type int_type;
        typedef typename Traits::int_type                               int_type;

        basic_tie_buf (
            ::std::basic_streambuf<Elem, Traits> *sbuf,
            ::std::basic_ofstream <Elem, Traits> *fstm)
            : _sbuf (sbuf)
            , _fstm (fstm)
        {}

        ::std::basic_streambuf<Elem, Traits>* sbuf () const
        {
            return _sbuf;
        }

        int_type log (int_type c, const Elem prefix[])
        {
            static int_type last_ch = '\n';

            bool is_err = false;
            if ('\n' == last_ch)
            {
                size_t length = strlen (prefix);
                if (_fstm->rdbuf ()->sputn (prefix, length) != length)
                {
                    is_err = true;
                }
            }
            if (is_err) return EOF;

            last_ch = _fstm->rdbuf ()->sputc (Elem (c));

            return last_ch;
        }

    protected:

        virtual int sync () override
        {
            _fstm->rdbuf ()->pubsync ();
            return _sbuf->pubsync ();
        }

        virtual int_type overflow (int_type c) override
        {
            return log (_sbuf->sputc (Elem (c)), "<< ");
        }


        virtual int_type underflow () override
        {
            return _sbuf->sgetc ();
        }

        virtual int_type uflow () override
        {
            return log (_sbuf->sbumpc (), ">> ");
        }

    };

    typedef basic_tie_buf<char,    ::std::char_traits<char> >     tie_buf;
    typedef basic_tie_buf<wchar_t, ::std::char_traits<wchar_t> >  tie_wbuf;


    //class TemporaryFilebuf : public std::filebuf
    //{
    //    std::ostream&   myStream;
    //    std::streambuf* mySavedStreambuf;
    //
    //public:
    //    TemporaryFilebuf(
    //        std::ostream& toBeChanged,
    //        std::string const& filename )
    //        : std::filebuf (filename.c_str (), std::ios_base::out)
    //        , myStream (toBeChanged )
    //        , mySavedStreambuf (toBeChanged.rdbuf())
    //    {
    //        toBeChanged.rdbuf( this );
    //    }
    //
    //    ~TemporaryFilebuf()
    //    {
    //        myStream.rdbuf( mySavedStreambuf );
    //    }
    //};

}

#endif // TIE_BUFFER_H_
