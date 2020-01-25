#pragma once

#include <algorithm>
#include <array>
#include <functional>
#include <mutex>
#include <string>
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

template<class T, u32 Size>
struct HashTable
{
private:
    std::array<T, Size> table;

public:

    //void clear()
    //{
    //    table.fill(T());
    //}

    T* operator[](Key key)
    {
        return &table[u32(key) & (Size - 1)];
    }
};

constexpr std::array<Square, SQ_NO> SQ
{
    SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
};
constexpr std::array<std::array<Value, PT_NO>, 2> PieceValues
{{
    { VALUE_MG_PAWN, VALUE_MG_NIHT, VALUE_MG_BSHP, VALUE_MG_ROOK, VALUE_MG_QUEN, VALUE_ZERO, VALUE_ZERO },
    { VALUE_EG_PAWN, VALUE_EG_NIHT, VALUE_EG_BSHP, VALUE_EG_ROOK, VALUE_EG_QUEN, VALUE_ZERO, VALUE_ZERO }
}};

// Case-insensitive comparator for char

inline bool case_insensitive_less(const unsigned char c1, const unsigned char c2)
{
    return
        //toupper(c1) < toupper(c2);
        tolower(c1) < tolower(c2);
}

inline bool case_insensitive_more(const unsigned char c1, const unsigned char c2)
{
    return
        //toupper(c1) > toupper(c2);
        tolower(c1) > tolower(c2);
}

inline bool case_insensitive_equal(const unsigned char c1, const unsigned char c2)
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
        return lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(), case_insensitive_less);
    }
};

struct CaseInsensitiveMoreComparer
    : public std::binary_function<std::string&, std::string&, bool>
{
    bool operator()(const std::string &s1, const std::string &s2) const
    {
        //return stricmp(s1.c_str(), s2.c_str()) > 0;
        return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(), case_insensitive_more);
    }
};

struct CaseInsensitiveEqualComparer
    : public std::binary_function<std::string&, std::string&, bool>
{
    bool operator()(const std::string &s1, const std::string &s2) const
    {
        //return stricmp(s1.c_str(), s2.c_str()) == 0;
        return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(), case_insensitive_equal);
    }
};

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

inline bool white_spaces(const std::string &str)
{
    return str.empty()
        || str.find_first_not_of(" \t\n") == std::string::npos
        || str == "<empty>";
}

inline std::string& to_lower(std::string &str)
{
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}
inline std::string& to_upper(std::string &str)
{
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}
inline std::string& toggle(std::string &str)
{
    std::transform(str.begin(), str.end(), str.begin(),
                   [](int c) -> int
                   { return islower(c) ? toupper(c) : tolower(c); });
    return str;
}

inline std::string& ltrim(std::string &str)
{
    str.erase(str.begin(),
              std::find_if(str.begin(), str.end(),
                           std::not1(std::function<bool(const std::string::value_type&)>(::isspace))));
    return str;
}
inline std::string& rtrim(std::string &str)
{
    str.erase(std::find_if(str.rbegin(), str.rend(),
                           std::not1(std::function<bool(const std::string::value_type&)>(::isspace))).base(),
              str.end());
    return str;
}
inline std::string& trim(std::string &str)
{
    return ltrim(rtrim(str));
}
inline std::string append_path(const std::string &base_path, const std::string &file_path)
{
    return base_path[base_path.length() - 1] != '/' ?
            base_path + '/' + file_path :
            base_path + file_path;
}

//template<class T>
//inline void replace(T &container,
//                    const typename T::value_type &old_value,
//                    const typename T::value_type &new_value)
//{
//    std::replace(container.begin(), container.end(), old_value, new_value);
//}

//inline std::vector<std::string> split(const std::string str, char delimiter = ' ', bool keep_empty = true, bool do_trim = false)
//{
//    std::vector<std::string> tokens;
//    std::istringstream iss{str};
//    while (iss.good())
//    {
//        std::string token;
//        const bool fail = !std::getline(iss, token, delimiter);
//        if (do_trim)
//        {
//            token = trim(token);
//        }
//        if (   keep_empty
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

//inline void erase_substring(std::string &str, const std::string &sub)
//{
//    std::string::size_type pos;
//    while ((pos = str.find(sub)) != std::string::npos)
//    {
//        str.erase(pos, sub.length());
//    }
//}
//
//inline void erase_substrings(std::string &str, const std::vector<std::string> &sub_list)
//{
//    std::for_each(sub_list.begin(), sub_list.end(), std::bind(erase_substring, std::ref(str), std::placeholders::_1));
//}
//
//inline void erase_extension(std::string &filename)
//{
//    std::string::size_type pos = filename.find_last_of('.');
//    if (pos != std::string::npos)
//    {
//        //filename = filename.substr(0, pos);
//        filename.erase(pos, std::string::npos);
//    }
//}
