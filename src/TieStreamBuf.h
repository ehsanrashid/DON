#pragma once

#include <streambuf>

// Our fancy logging facility.
// The trick here is to replace cin.rdbuf() and cout.rdbuf() with
// two BasicTieStreamBuf objects that tie cin and cout to a file stream.
// Can toggle the logging of std::cout and std:cin at runtime whilst preserving
// usual I/O functionality, all without changing a single line of code!
// Idea from http://groups.google.com/group/comp.lang.c++/msg/1d941c0f26ea0d81
class TieStreamBuf
    : public std::streambuf
{
private:

    int_type write(int_type ch, const std::string &prefix)
    {
        // Previous character
        static int_type pch = '\n';

        if ('\n' == pch)
        {
            //if (
            writSB->sputn(prefix.c_str(), prefix.length());
            //    != prefix.length()) return EOF;
        }
        return pch = writSB->sputc(char(ch));
    }

protected:

    TieStreamBuf(const TieStreamBuf&) = delete;
    TieStreamBuf& operator=(const TieStreamBuf&) = delete;

    int      sync() override { return writSB->pubsync(), readSB->pubsync(); }
    int_type overflow(int_type ch) override { return write(readSB->sputc(char(ch)), "<< "); }
    int_type underflow() override { return readSB->sgetc(); }
    int_type uflow() override { return write(readSB->sbumpc(), ">> "); }

public:

    std::streambuf *readSB
        ,          *writSB;

    TieStreamBuf(std::streambuf *rSB,
                 std::streambuf *wSB)
        : readSB(rSB)
        , writSB(wSB)
    {}

};

