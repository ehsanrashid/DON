#pragma once

#include <streambuf>
#include <cstring>

namespace std {

    // Our fancy logging facility. The trick here is to replace cin.rdbuf() and cout.rdbuf()
    // with two basic_tie_buf objects that tie cin and cout to a file stream.
    // Can toggle the logging of std::cout and std:cin at runtime whilst preserving
    // usual I/O functionality, all without changing a single line of code!
    // Idea from http://groups.google.com/group/comp.lang.c++/msg/1d941c0f26ea0d81

    template<typename Elem, typename Traits>
    class basic_tie_buf
        : public basic_streambuf<Elem, Traits>
    {
    public:

        typedef typename Traits::int_type   int_type;

    private:
        // MSVC requires split streambuf for cin and cout
        basic_streambuf<Elem, Traits> *_strmbuf1;
        basic_streambuf<Elem, Traits> *_strmbuf2;

        int_type lastCh = '\n';

    public:

        basic_tie_buf(basic_streambuf<Elem, Traits> *strmbuf1,
                      basic_streambuf<Elem, Traits> *strmbuf2)
            : _strmbuf1(strmbuf1)
            , _strmbuf2(strmbuf2)
        {}

        basic_streambuf<Elem, Traits>* streambuf() const { return _strmbuf1; }

        int_type write(int_type c, const Elem *prefix)
        {
            if ('\n' == lastCh)
            {
                streamsize length = strlen(prefix);
                if (_strmbuf2->sputn(prefix, length) != length)
                {
                    return EOF; // Error
                }
            }

            return lastCh = _strmbuf2->sputc(Elem(c));
        }

    protected:

        basic_tie_buf(const basic_tie_buf&) = delete;
        basic_tie_buf& operator=(const basic_tie_buf&) = delete;

        int sync() override
        {
            return _strmbuf2->pubsync(), _strmbuf1->pubsync();
        }

        int_type overflow(int_type c) override
        {
            return write(_strmbuf1->sputc(Elem(c)), "<< ");
        }

        int_type underflow() override
        {
            return _strmbuf1->sgetc();
        }

        int_type uflow() override
        {
            return write(_strmbuf1->sbumpc(), ">> ");
        }

    };

    typedef basic_tie_buf<char   , char_traits<char   > >  tie_buf;
    typedef basic_tie_buf<wchar_t, char_traits<wchar_t> >  tie_wbuf;
}
