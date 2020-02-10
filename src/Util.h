#pragma once

#include <algorithm>
#include <functional>
#include <mutex>
#include <string>
#include <iostream>
//#include <vector>

#include "Type.h"

enum OutputState : u08
{
    OS_LOCK,
    OS_UNLOCK,
};

/// Used to serialize access to std::cout to avoid multiple threads writing at the same time.
inline std::ostream& operator<<(std::ostream &os, OutputState state)
{
    static std::mutex mtx;

    switch (state)
    {
    case OutputState::OS_LOCK:   mtx.lock();   break;
    case OutputState::OS_UNLOCK: mtx.unlock(); break;
    default: break;
    }
    return os;
}

#define sync_cout std::cout << OS_LOCK
#define sync_endl std::endl << OS_UNLOCK

// Case-insensitive comparator for char

inline bool caseInsensitiveLess(const unsigned char c1, const unsigned char c2)
{
    return
        //toupper(c1) < toupper(c2);
        tolower(c1) < tolower(c2);
}

inline bool caseInsensitiveMore(const unsigned char c1, const unsigned char c2)
{
    return
        //toupper(c1) > toupper(c2);
        tolower(c1) > tolower(c2);
}

inline bool caseInsensitiveEqual(const unsigned char c1, const unsigned char c2)
{
    return
        //toupper(c1) == toupper(c2);
        tolower(c1) == tolower(c2);
}

// Case-insensitive comparator for string

struct CaseInsensitiveLessComparer
    : public std::binary_function<std::string&, std::string&, bool>
{
    bool operator()(const std::string &s1, const std::string &s2) const
    {
        //return stricmp(s1.c_str(), s2.c_str()) < 0;
        return lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(), caseInsensitiveLess);
    }
};

struct CaseInsensitiveMoreComparer
    : public std::binary_function<std::string&, std::string&, bool>
{
    bool operator()(const std::string &s1, const std::string &s2) const
    {
        //return stricmp(s1.c_str(), s2.c_str()) > 0;
        return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(), caseInsensitiveMore);
    }
};

struct CaseInsensitiveEqualComparer
    : public std::binary_function<std::string&, std::string&, bool>
{
    bool operator()(const std::string &s1, const std::string &s2) const
    {
        //return stricmp(s1.c_str(), s2.c_str()) == 0;
        return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(), caseInsensitiveEqual);
    }
};

// Return the sign of a number (-1, 0, 1)
template<class T>
constexpr i32 sign(const T val)
{
    return (T(0) < val) - (val < T(0));
}

template<class T>
const T& clamp(const T &v, const T &minimum, const T &maximum)
{
    return (minimum > v) ? minimum :
           (v > maximum) ? maximum : v;
}

inline bool whiteSpaces(const std::string &str)
{
    return str.empty()
        || str.find_first_not_of(" \t\n") == std::string::npos
        || str == "<empty>";
}

inline std::string& toLower(std::string &str)
{
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}
inline std::string& toUpper(std::string &str)
{
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}
inline std::string& toggleCase(std::string &str)
{
    std::transform(str.begin(), str.end(), str.begin(),
                   [](int c) -> int
                   { return islower(c) ? toupper(c) : tolower(c); });
    return str;
}

inline std::string& leftTrim(std::string &str)
{
    str.erase(str.begin(),
              std::find_if(str.begin(), str.end(),
                           std::not1(std::function<bool(const std::string::value_type&)>(::isspace))));
    return str;
}
inline std::string& rihtTrim(std::string &str)
{
    str.erase(std::find_if(str.rbegin(), str.rend(),
                           std::not1(std::function<bool(const std::string::value_type&)>(::isspace))).base(),
              str.end());
    return str;
}
inline std::string& fullTrim(std::string &str)
{
    return leftTrim(rihtTrim(str));
}
inline std::string appendPath(const std::string &basePath, const std::string &filePath)
{
    return basePath[basePath.length() - 1] != '/' ?
            basePath + '/' + filePath :
            basePath + filePath;
}

//template<class T>
//inline void replace(T &container,
//                    const typename T::value_type &oldValue,
//                    const typename T::value_type &newValue)
//{
//    std::replace(container.begin(), container.end(), oldValue, newValue);
//}

//inline std::vector<std::string> split(const std::string str, char delimiter = ' ', bool keepEmpty = true, bool doTrim = false)
//{
//    std::vector<std::string> tokens;
//    std::istringstream iss{ str };
//    while (iss.good())
//    {
//        std::string token;
//        const bool fail = !std::getline(iss, token, delimiter);
//        if (doTrim)
//        {
//            token = fullTrim(token);
//        }
//        if (   keepEmpty
//            || !token.empty())
//        {
//            tokens.push_back(token);
//        }
//        if (fail)
//        {
//            break;
//        }
//    }
//
//    return tokens;
//}

//inline void eraseSubstring(std::string &str, const std::string &sub)
//{
//    std::string::size_type pos;
//    while ((pos = str.find(sub)) != std::string::npos)
//    {
//        str.erase(pos, sub.length());
//    }
//}
//
//inline void eraseSubstrings(std::string &str, const std::vector<std::string> &sub_list)
//{
//    std::for_each(sub_list.begin(), sub_list.end(), std::bind(eraseSubstring, std::ref(str), std::placeholders::_1));
//}
//
//inline void eraseExtension(std::string &filename)
//{
//    std::string::size_type pos = filename.find_last_of('.');
//    if (pos != std::string::npos)
//    {
//        //filename = filename.substr(0, pos);
//        filename.erase(pos, std::string::npos);
//    }
//}

/*
// Nullfunction is a function that does nothing, allows usage of shared_ptr with stack allocated or static objects.

template<typename T>
struct NullUnaryFunction
    : public std::unary_function<T*, void>
{
    void operator()(const T*) const
    {}
};

template<typename T>
struct NullBinaryFunction
    : public std::binary_function<T*, T*, void>
{
    void operator()(const T*, const T*) const
    {}
};
*/

