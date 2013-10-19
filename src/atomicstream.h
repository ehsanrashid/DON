//#pragma once
#ifndef ATOMIC_STREAM_H_
#define ATOMIC_STREAM_H_

#include <sstream>
#include <iostream>
#include <mutex>
#include "noncopyable.h"
//#include "functor.h"

namespace std {

    // atomic_stream is a full expression stream accumulator for ostream <char>
    // It is thread safe stream for printing output.


    //typedef class atomic_stream sealed
    //{
    //
    //private:
    //    // output stream for atomic_stream
    //    //std::shared_ptr<std::ostream>   _out_stm;
    //    std::ostream     &_out_stm;
    //
    //    // std::stringstream is not copyable, so copies are already forbidden
    //    std::ostringstream _os_stm;
    //
    //public:
    //
    //    explicit atomic_stream (std::ostream &out_stm = std::cout)
    //        //: _out_stm(std::shared_ptr<std::ostream> (&out_stm, std::unary_nullfunctor<std::ostream> ()))
    //        : _out_stm (out_stm)
    //    {}
    //
    //    ~atomic_stream ()
    //    {
    //        (*this) ();
    //    }
    //
    //    template<class T>
    //    // Accumulate into a non-shared stringstream, no threading issues
    //    atomic_stream& operator<< (const T &v)
    //    {
    //        _os_stm << v;
    //        return *this;
    //    }
    //
    //    //template<class T>
    //    //atomic_stream& operator<< (T& (*manip) (T &))
    //    //{
    //    //    manip (_os_stm);
    //    //    return *this;
    //    //}
    //
    //    // this is the type of std::cout
    //    //typedef std::basic_ostream<char, std::char_traits<char> > ostream;
    //    // this is the function signature of std::endl
    //    typedef std::ostream& (*ostream_manipulator) (std::ostream &);
    //
    //    // define an operator<< to take in std::endl
    //    //template<>
    //    atomic_stream& operator<< (ostream_manipulator manip)
    //    {
    //        // call the function, but we cannot return it's value
    //        manip (_os_stm);
    //        return *this;
    //    }
    //
    //    // function that takes a custom stream, and returns it
    //    typedef atomic_stream& (*acc_manipulator) (atomic_stream &);
    //
    //    // take in a function with the custom signature
    //    //template<>
    //    atomic_stream& operator<< (acc_manipulator manip)
    //    {
    //        // call the function, and return it's value
    //        return manip (*this);
    //    }
    //
    //    // define the custom endl for this stream.
    //    // note how it matches the `atomic_stream` function signature
    //    static atomic_stream& endl (atomic_stream &atom)
    //    {
    //        // put a new line
    //        atom._os_stm.put ('\n');
    //        // do other stuff with the stream
    //        // std::cout, for example, will flush the stream
    //        //atom << "Called MyStream::endl!" << std::endl;
    //        return atom;
    //    }
    //
    //    // Write the whole shebang in one go & also flush
    //    atomic_stream& operator() ()
    //    {
    //        _out_stm << _os_stm.rdbuf () << std::flush;
    //        return *this;
    //    }
    //
    //} atom;

    //template<class T>
    //std::atom& operator<< (std::atom& os, T& (*manip) (T &))
    //{
    //    //manip (os._os_stm);
    //    return os;
    //}

    //typedef std::ostream& (*ostream_manipulator)(std::ostream &);
    //template<>
    //std::atom& operator<< (std::atom& os, ostream_manipulator pf)
    //{
    //    //os.operator<< <ostream_manipulator> (os, pf);
    //    return os;
    //}


    typedef class atomic_stream sealed
        : public std::ostringstream
        , public std::noncopyable
    {

    private:
        // output stream for atomic_stream
        std::ostream     &_out_stm;

    public:

        explicit atomic_stream (std::ostream &out_stm = std::cout)
            : std::ostringstream ()
            , _out_stm (out_stm)
        {}

        ~atomic_stream ()
        {
            (*this) ();
        }

        // Write the whole shebang in one go & also flush
        atomic_stream& operator() ()
        {
            {
                // acquire lock
                std::unique_lock<std::mutex> lock;
                _out_stm << str () << std::flush;
                clear ();
                // release lock
            }
            return *this;
        }

    } atom;

    // using a temporary instead of returning one from a function avoids any issues with copies
    //atom (out_stm) << val1 << val2 << val3;

}

#endif
