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
        basic_streambuf<Elem, Traits> *_strmbuf1;
        basic_streambuf<Elem, Traits> *_strmbuf2;

    public:

        typedef typename Traits::int_type   int_type;

        basic_tie_buf (basic_streambuf<Elem, Traits> *strmbuf1,
                       basic_streambuf<Elem, Traits> *strmbuf2)
            : _strmbuf1 (strmbuf1)
            , _strmbuf2 (strmbuf2)
        {}

        basic_streambuf<Elem, Traits>* streambuf () const { return _strmbuf1; }

        int_type write (int_type c, const Elem *prefix)
        {
            static int_type last_ch = '\n';
            
            if ('\n' == last_ch)
            {
                streamsize length = strlen (prefix);
                if (_strmbuf2->sputn (prefix, length) != length)
                {
                    return EOF; // Error
                }
            }

            return last_ch = _strmbuf2->sputc (Elem (c));
        }

    protected:

        basic_tie_buf (const basic_tie_buf&) = delete;
        basic_tie_buf& operator= (const basic_tie_buf&) = delete;

        int sync () override
        {
            return _strmbuf2->pubsync (), _strmbuf1->pubsync ();
        }

        int_type overflow (int_type c) override
        {
            return write (_strmbuf1->sputc (Elem (c)), "<< ");
        }

        int_type underflow () override
        {
            return _strmbuf1->sgetc ();
        }

        int_type uflow () override
        {
            return write (_strmbuf1->sbumpc (), ">> ");
        }

    };

    typedef basic_tie_buf<char,    char_traits<char> >     tie_buf;
    typedef basic_tie_buf<wchar_t, char_traits<wchar_t> >  tie_wbuf;
}

#endif // _TIE_BUFFER_H_INC_
