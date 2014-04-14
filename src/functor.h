#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _FUNCTOR_H_INC_
#define _FUNCTOR_H_INC_

#include <functional>
#include <cctype>

namespace std {

    // nullfunctor is a functor that does nothing, allows usage of shared_ptr with stack allocated or static objects. 
    // Taken from boost/serialization/shared_ptr.hpp

    template<class T>
    struct unary_nullfunctor : public unary_function<T*, void>
    {
        void operator() (const T *op) const
        {}
    };

    template<class T>
    struct binary_nullfunctor : public binary_function<T*, T*, void>
    {
        void operator() (const T *op1, const T *op2) const
        {}
    };

    // Case-insensitive less comparator for char
    inline bool NoCaseLess (const unsigned char c1, const unsigned char c2)
    {
        //return toupper (c1) < toupper (c2);
        return tolower (c1) < tolower (c2);
    }

    // Case-insensitive less comparator for string
    struct NoCaseLessComparer : public binary_function<string&, string&, bool>
    {
        bool operator() (const string &s1, const string &s2) const
        {
            //string::const_iterator itr1 = s1.begin ();
            //string::const_iterator itr2 = s2.begin ();
            //while (itr1 != s1.end () && itr2 != s2.end ()
            //    && toupper (*itr1) == toupper (*itr2))
            //{
            //    ++itr1;
            //    ++itr2;
            //}
            //return (itr1 == s1.end ()) ? itr2 != s2.end () : toupper (*itr1) < toupper (*itr2);

            //return stricmp(s1.c_str (), s2.c_str ()) < 0;

            return lexicographical_compare (s1.begin (), s1.end (), s2.begin (), s2.end (), NoCaseLess);
        }
    };

    //// Case-insensitive Equal comparator for char
    //inline bool NoCaseEqual (const unsigned char c1, const unsigned char c2)
    //{
    //    //return toupper (c1) == toupper (c2);
    //    return tolower (c1) == tolower (c2);
    //}
    //// Case-insensitive Equal comparator for string
    //struct NoCaseEqualComparer : public binary_function<string&, string&, bool>
    //{
    //    bool operator() (const string &s1, const string &s2) const
    //    {
    //        return stricmp(s1.c_str (), s2.c_str ()) == 0;
    //        //return lexicographical_compare (s1.begin (), s1.end (), s2.begin (), s2.end (), NoCaseEqual);
    //    }
    //};

}

#endif // _FUNCTOR_H_INC_
