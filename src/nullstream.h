#ifndef _NULLSTREAM_H_INC_
#define _NULLSTREAM_H_INC_

// Copyright (c) 2006 - 2010
// Seweryn Habdank-Wojewodzki
//
// Distributed under the Boost Software License, Version 1.0.
// ( copy at http://www.boost.org/LICENSE_1_0.txt )
//
// Copyright Maciej Sobczak, 2002
//
// Permission to copy, use, modify, sell and distribute this software
// is granted provided this copyright notice appears in all copies.
// This software is provided "as is" without express or implied
// warranty, and with no claim as to its suitability for any purpose.

#include <streambuf>
#include <ostream>
#include "noncopyable.h"

namespace std {

    // generic null stream buffer class
    template <class CharT, class Traits = char_traits<CharT> >
    class basic_null_buffer
        : public basic_streambuf<CharT, Traits>
        , public noncopyable
    {
    private:

    public:
        typedef typename basic_streambuf<CharT, Traits>::int_type int_type;

        basic_null_buffer() {}

        //virtual int_type overflow (int_type c) override
        //{
        //    // just ignore the character
        //    return typename Traits::not_eof (c);
        //}
    };

    // generic null output stream class
    template <class CharT, class Traits = char_traits<CharT> >
    class basic_null_stream
        : private basic_null_buffer<CharT, Traits>
        , public basic_ostream<CharT, Traits>
    {

    public:

        basic_null_stream()
            // C++98 standard allows that construction
            // 12.6.2/7
            : basic_ostream<CharT, Traits> (this)
        {}

    };

    template<class CharT, class Traits, class T>
    inline basic_null_stream<CharT, Traits>& operator<< (basic_null_stream<CharT, Traits> &nstream, T const &)
    {
        return nstream;
    }

    template<class CharT, class Traits>
    inline basic_null_stream<CharT, Traits>& operator<< (basic_null_stream<CharT, Traits> &nstream, basic_ostream<CharT, Traits> &(basic_ostream<CharT, Traits> &))
    {
        return nstream;
    }

    // helper declarations for narrow and wide streams
    typedef basic_null_stream<char>     null_stream;
    typedef basic_null_stream<wchar_t>  null_wstream;

}

#endif // _NULLSTREAM_H_INC_
