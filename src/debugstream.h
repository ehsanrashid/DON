//#pragma once
#ifndef DEBUG_STREAM_H_
#define DEBUG_STREAM_H_

#include <streambuf>
#include <fstream>

namespace std {


    class debug_streambuf : public std::filebuf
    {

    private:

    public:

        debug_streambuf() { std::filebuf::open("NUL", std::ios_base::out); }

        void open(const char fname[]);
        void close()
        {
            std::filebuf::close();
        } 

        virtual int sync();
    };

    void debug_streambuf::open(const char fname[])
    {
        close();
        std::filebuf::open(fname ? fname : "NUL", std::ios_base::out | std::ios_base::app | std::ios_base::trunc);
    } 
    int debug_streambuf::sync()
    {
        //int count = out_waiting();
        return std::filebuf::sync(); 
    }


    class debug_stream : public std::ostream
    {

    public:

        debug_stream()
            : std::ostream(new debug_streambuf())
            //, ios (0)
        {}

        ~debug_stream() { delete rdbuf(); }

        //void open(const char fname[] = 0) { _buf.open(fname); }
        //void close() { std::_buf.close(); }

    };

}

#endif // DEBUG_STREAM_H_
