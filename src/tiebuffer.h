//#pragma once
#ifndef TIE_BUFFER_H_
#define TIE_BUFFER_H_

#include <streambuf>
#include <fstream>
#include "noncopyable.h"

namespace std {

    template<class Elem, class Traits>
    class basic_tie_buf
        : public basic_streambuf<Elem, Traits>
        , public noncopyable
    {

    private:
        basic_streambuf<Elem, Traits> *_sbuf;
        basic_ofstream <Elem, Traits> *_fstm;

    public:

        //typedef typename basic_streambuf<Elem, Traits>::int_type int_type;
        typedef typename Traits::int_type                               int_type;

        basic_tie_buf (
            basic_streambuf<Elem, Traits> *sbuf,
            basic_ofstream <Elem, Traits> *fstm)
            : _sbuf (sbuf)
            , _fstm (fstm)
        {}

        inline basic_streambuf<Elem, Traits>* sbuf () const
        {
            return _sbuf;
        }

        inline int_type write (int_type c, const Elem prefix[])
        {
            static int_type last_ch = '\n';

            bool error = false;
            if ('\n' == last_ch)
            {
                size_t length = strlen (prefix);
                if (size_t (_fstm->rdbuf ()->sputn (prefix, length)) != length)
                {
                    error = true;
                }
            }
            if (error) return EOF;

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
            return write (_sbuf->sputc (Elem (c)), "<< ");
        }

        virtual int_type underflow () override
        {
            return _sbuf->sgetc ();
        }

        virtual int_type uflow () override
        {
            return write (_sbuf->sbumpc (), ">> ");
        }

    };

    typedef basic_tie_buf<char,    char_traits<char> >     tie_buf;
    typedef basic_tie_buf<wchar_t, char_traits<wchar_t> >  tie_wbuf;


    //class TemporaryFilebuf : public filebuf
    //{
    //    ostream&   myStream;
    //    streambuf* mySavedStreambuf;
    //
    //public:
    //    TemporaryFilebuf(
    //        ostream& toBeChanged,
    //        string const& filename )
    //        : filebuf (filename.c_str (), ios_base::out)
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
