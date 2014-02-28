#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _ATOMIC_STREAM_H_INC_
#define _ATOMIC_STREAM_H_INC_

#include <sstream>
#include <iostream>
//#include <mutex>
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
    //    //shared_ptr<ostream>   _out_stm;
    //    ostream     &_out_stm;
    //
    //    // stringstream is not copyable, so copies are already forbidden
    //    ostringstream _os_stm;
    //
    //public:
    //
    //    explicit atomic_stream (ostream &out_stm = cout)
    //        //: _out_stm(shared_ptr<ostream> (&out_stm, unary_nullfunctor<ostream> ()))
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
    //    // this is the type of cout
    //    //typedef basic_ostream<char, char_traits<char> > ostream;
    //    // this is the function signature of endl
    //    typedef ostream& (*ostream_manipulator) (ostream &);
    //
    //    // define an operator<< to take in endl
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
    //    static atomic_stream& endl (atomic_stream &ats)
    //    {
    //        // put a new line
    //        ats._os_stm.put ('\n');
    //        // do other stuff with the stream
    //        // cout, for example, will flush the stream
    //        //ats << "Called MyStream::endl!" << endl;
    //        return ats;
    //    }
    //
    //    // Write the whole shebang in one go & also flush
    //    atomic_stream& operator() ()
    //    {
    //        _out_stm << _os_stm.rdbuf () << flush;
    //        return *this;
    //    }
    //
    //} ats;
    //
    //template<class T>
    //ats& operator<< (ats &os, T& (*manip) (T &))
    //{
    //    //manip (os._os_stm);
    //    return os;
    //}
    //
    //typedef ostream& (*ostream_manipulator)(ostream &);
    //template<>
    //ats& operator<< (ats &os, ostream_manipulator pf)
    //{
    //    //os.operator<< <ostream_manipulator> (os, pf);
    //    return os;
    //}

    typedef class atomic_stream sealed
        : public ostringstream
        , public noncopyable
    {

    private:
        // output stream for atomic_stream
        ostream     &_out_stm;

    public:

        explicit atomic_stream (ostream &out_stm = cout)
            : ostringstream ()
            , _out_stm (out_stm)
        {}

        ~atomic_stream ()
        {
            (*this) ();
        }

        // Write the whole shebang in one go and also flush
        inline atomic_stream& operator() ()
        {
            {
<<<<<<< HEAD
                // acquire lock
<<<<<<< HEAD
                unique_lock<mutex> lock;
=======
=======
                // Acquire lock
>>>>>>> origin/minGW
                //unique_lock<mutex> lock;
>>>>>>> origin/PieceList
                _out_stm << str () << std::flush;
                //clear ();
                // Release lock
            }
            return *this;
        }

    } ats;

    // using a temporary instead of returning one from a function avoids any issues with copies
    //ats (out_stm) << val1 << val2 << val3;

}

#endif // _ATOMIC_STREAM_H_INC_
