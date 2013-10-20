// Copyright (c) 2006 - 2010
// Seweryn Habdank-Wojewodzki
//
// Distributed under the Boost Software License, Version 1.0.
// ( copy at http://www.boost.org/LICENSE_1_0.txt )

// Copyright Maciej Sobczak, 2002
//
// Permission to copy, use, modify, sell and distribute this software
// is granted provided this copyright notice appears in all copies.
// This software is provided "as is" without express or implied
// warranty, and with no claim as to its suitability for any purpose.

//#pragma once
#ifndef NULLSTREAM_H_
#define NULLSTREAM_H_

#include <streambuf>
#include <ostream>
#include "noncopyable.h"

namespace std {

    // generic null stream buffer class
    template <class charT, class Traits = std::char_traits<charT> >
    class basic_null_buffer
        : public std::basic_streambuf<charT, Traits>
        , public std::noncopyable
    {

    public:
        typedef typename std::basic_streambuf<charT, Traits>::int_type int_type;

        basic_null_buffer() {}

    private:

        virtual int_type overflow (int_type c) override
        {
            // just ignore the character
            return typename Traits::not_eof (c);
        }
    };


    // generic null output stream class
    template <class charT, class Traits = std::char_traits<charT> >
    class basic_null_stream sealed
        : private std::basic_null_buffer<charT, Traits>
        , public std::basic_ostream<charT, Traits>
        , public std::noncopyable
    {

    public:

        basic_null_stream()
            // C++98 standard allows that construction
            // 12.6.2/7
            : std::basic_ostream<charT, Traits>(this)
        {}

    };


    template<class charT, class Traits, class T>
    inline std::basic_null_stream<charT, Traits>& operator<< (
        std::basic_null_stream<charT, Traits> &nstream, T const &)
    {
        return nstream;
    }

    template<class charT, class Traits>
    inline std::basic_null_stream<charT, Traits>& operator<< (
        std::basic_null_stream<charT, Traits> &nstream, std::basic_ostream<charT, Traits> &(std::basic_ostream<charT, Traits> &))
    {
        return nstream;
    }


    // helper declarations for narrow and wide streams
    typedef std::basic_null_stream<char>     null_stream;
    typedef std::basic_null_stream<wchar_t>  null_wstream;

}
#endif // NULLSTREAM_H_