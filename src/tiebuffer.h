#ifndef _TIE_BUFFER_H_INC_
#define _TIE_BUFFER_H_INC_

#include <streambuf>
#include <cstring>

namespace std {

    template<class Elem, class Traits>
    class basic_tie_buf
        : public basic_streambuf<Elem, Traits>
    {

    private:
        basic_streambuf<Elem, Traits> *_strmbuf;
        basic_streambuf<Elem, Traits> *_logstrmbuf;

    public:

        typedef typename Traits::int_type   int_type;

        basic_tie_buf (
            basic_streambuf<Elem, Traits> *strmbuf,
            basic_streambuf<Elem, Traits> *logstrmbuf)
            : _strmbuf (strmbuf)
            , _logstrmbuf (logstrmbuf)
        {}

        basic_streambuf<Elem, Traits>* streambuf () const { return _strmbuf; }

        int_type write (int_type c, const Elem *prefix)
        {
            static int_type last_ch = '\n';
            
            if ('\n' == last_ch)
            {
                streamsize length = strlen (prefix);
                if (_logstrmbuf->sputn (prefix, length) != length)
                {
                    return EOF; // Error
                }
            }

            return last_ch = _logstrmbuf->sputc (Elem (c));
        }

    protected:

        basic_tie_buf (const basic_tie_buf&) = delete;
        basic_tie_buf& operator= (const basic_tie_buf&) = delete;

        int sync () override
        {
            return _logstrmbuf->pubsync (), _strmbuf->pubsync ();
        }

        int_type overflow (int_type c) override
        {
            return write (_strmbuf->sputc (Elem (c)), "<< ");
        }

        int_type underflow () override
        {
            return _strmbuf->sgetc ();
        }

        int_type uflow () override
        {
            return write (_strmbuf->sbumpc (), ">> ");
        }

    };

    typedef basic_tie_buf<char,    char_traits<char> >     tie_buf;
    typedef basic_tie_buf<wchar_t, char_traits<wchar_t> >  tie_wbuf;
}

#endif // _TIE_BUFFER_H_INC_
