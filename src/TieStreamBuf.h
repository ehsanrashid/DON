#pragma once

#include <streambuf>
#include <string_view>

// Fancy logging facility.
// The trick here is to replace cin.rdbuf() and cout.rdbuf() with two
// TieStreamBuf objects that tie std::cin and std::cout to a out file stream.
// Can toggle the logging of std::cout and std:cin at runtime whilst preserving
// usual I/O functionality, all without changing a single line of code!
// Idea from http://groups.google.com/group/comp.lang.c++/msg/1d941c0f26ea0d81
class TieStreamBuf :
    public std::streambuf {

public:
    TieStreamBuf(std::streambuf *rsb, std::streambuf *wsb) :
        rstreambuf{ rsb },
        wstreambuf{ wsb }
    {}
    //TieStreamBuf(TieStreamBuf const&) = delete;
    //TieStreamBuf& operator=(TieStreamBuf const&) = delete;

    int sync() override {
        return wstreambuf->pubsync(), rstreambuf->pubsync();
    }

    int_type overflow(int_type ch) override {
        return write(rstreambuf->sputc(char(ch)), "<< ");
    }

    int_type underflow() override {
        return rstreambuf->sgetc();
    }

    int_type uflow() override {
        return write(rstreambuf->sbumpc(), ">> ");
    }

    std::streambuf *rstreambuf;
    std::streambuf *wstreambuf;

private:
    int_type write(int_type ch, std::string_view prefix) {
        static int_type prevCh = '\n';

        if (prevCh == '\n') {
            //if (
            wstreambuf->sputn(prefix.data(), prefix.length());
            //    != prefix.length()) return EOF;
        }
        return prevCh = wstreambuf->sputc(char(ch));
    }
};
