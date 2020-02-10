#pragma once

#include <fstream>
#include <iostream>
#include <streambuf>
#include <cstring>
#include <string>

#include "Type.h"

// Our fancy logging facility.
// The trick here is to replace cin.rdbuf() and cout.rdbuf() with
// two BasicTieStreamBuf objects that tie cin and cout to a file stream.
// Can toggle the logging of std::cout and std:cin at runtime whilst preserving
// usual I/O functionality, all without changing a single line of code!
// Idea from http://groups.google.com/group/comp.lang.c++/msg/1d941c0f26ea0d81
template<typename Elem, typename Traits>
class BasicTieStreamBuf
    : public std::basic_streambuf<Elem, Traits>
{
public:

    typedef typename Traits::int_type   int_type;

    std::basic_streambuf<Elem, Traits> *rStreamBuf;
    std::basic_streambuf<Elem, Traits> *wStreamBuf;

    BasicTieStreamBuf(std::basic_streambuf<Elem, Traits> *rsb,
                      std::basic_streambuf<Elem, Traits> *wsb)
        : rStreamBuf(rsb)
        , wStreamBuf(wsb)
    {}

protected:

    BasicTieStreamBuf(const BasicTieStreamBuf&) = delete;
    BasicTieStreamBuf& operator=(const BasicTieStreamBuf&) = delete;

    int                     sync() override { return wStreamBuf->pubsync(), rStreamBuf->pubsync(); }
    int_type overflow(int_type ch) override { return write(rStreamBuf->sputc(Elem(ch)), "<< "); }
    int_type           underflow() override { return rStreamBuf->sgetc(); }
    int_type               uflow() override { return write(rStreamBuf->sbumpc(), ">> "); }

private:

    int_type write(int_type ch, const Elem *prefix)
    {
        // Last character
        static int_type _ch = '\n';

        if ('\n' == _ch)
        {
            wStreamBuf->sputn(prefix, strlen(prefix));
        }
        return _ch = wStreamBuf->sputc(Elem(ch));
    }

};

typedef BasicTieStreamBuf<char   , std::char_traits<char   >>  TieStreamBuf;
//typedef BasicTieStreamBuf<wchar_t, std::char_traits<wchar_t>> wTieStreamBuf;

// I/O Logger
class Logger
{
private:
    std::ofstream _ofStream;
    TieStreamBuf  _iTieStreamBuf;
    TieStreamBuf  _oTieStreamBuf;

public:

    Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    virtual ~Logger();

    void set(const std::string&);
};

extern std::string toString(const std::chrono::system_clock::time_point&);

template<typename Elem, typename Traits>
inline std::basic_ostream<Elem, Traits>&
operator<<(std::basic_ostream<Elem, Traits> &os, const std::chrono::system_clock::time_point &tp)
{
    os << toString(tp);
    return os;
}

// Debug functions used mainly to collect run-time statistics
extern void initializeDebug();
extern void debugHit(bool);
extern void debugHitOn(bool, bool);
extern void debugMeanOf(i64);
extern void debugPrint();

// Global Logger
extern Logger Log;
