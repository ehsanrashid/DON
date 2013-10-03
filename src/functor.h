//#pragma once
#ifndef FUNCTOR_H_
#define FUNCTOR_H_

#include <functional>
#include <cctype>

namespace std {

    // nullfunctor is a functor that does nothing, allows usage of shared_ptr with stack allocated or static objects. 
    // Taken from boost/serialization/shared_ptr.hpp

    template<class T>
    struct unary_nullfunctor : public std::unary_function<T *, void>
    {
        void operator() (const T *op) const
        {}
    };

    template<class T>
    struct binary_nullfunctor : public std::binary_function<T *, T *, void>
    {
        void operator() (const T *op1, const T *op2) const
        {}
    };


    // char case-insensitive less comparator
    struct   char_less_comparer : public std::binary_function<unsigned char, unsigned char, bool>
    {
        bool operator() (unsigned char c1, unsigned char c2) const
        {
            //return toupper(c1) < toupper(c2);
            return tolower (c1) < tolower (c2);
        }
    };

    // std::string case-insensitive less comparator
    struct string_less_comparer : public std::binary_function<std::string &, std::string &, bool>
    {
        bool operator() (const std::string &s1, const std::string &s2) const
        {
            //std::string::const_iterator itr1 = s1.begin();
            //std::string::const_iterator itr2 = s2.begin();
            //while (itr1 != s1.end() && itr2 != s2.end()
            //    && toupper(*itr1) == toupper(*itr2))
            //{
            //    ++itr1;
            //    ++itr2;
            //}
            //return (itr1 == s1.end()) ? itr2 != s2.end() : toupper(*itr1) < toupper(*itr2);

            // ---

            //return stricmp(s1.c_str(), s2.c_str()) < 0;
            return std::lexicographical_compare (s1.cbegin (), s1.cend (), s2.cbegin (), s2.cend (), char_less_comparer ());
        }
    };

    //// case-insensitive equal comparator for char
    //struct   charEqualComparer : public std::binary_function<unsigned char, unsigned char, bool>
    //{
    //    bool operator() (unsigned char c1, unsigned char c2) const
    //    {
    //        //return std::toupper(c1) == std::toupper(c2);
    //        return std::tolower(c1) == std::tolower(c2);
    //    }
    //};

    //// case-insensitive equal comparator for std::string
    //struct stringEqualComparer : public std::binary_function<std::string &, std::string &, bool>
    //{
    //    bool operator() (const std::string &s1, const std::string &s2) const
    //    {
    //        return stricmp(s1.c_str(), s2.c_str()) == 0;
    //        //return std::lexicographical_compare(s1.cbegin(), s1.cend(), s2.cbegin(), s2.cend(), charEqualComparer());
    //    }
    //};


}

#endif
