#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _XSTRING_H_INC_
#define _XSTRING_H_INC_

#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <sstream>
//#include <unordered_set>
//#include <sstream>  // stringstream in trim
//#include <stack>    // stack<> in reverse

#include "Platform.h"

namespace std {

    inline bool whitespace (const std::string &s)
    {
        if (s.empty ()) return true;
        for (size_t i = 0; i < s.length (); ++i)
        {
            if (!isspace (s[i])) return false;
        }
        return true;
    }

    //inline std::string& strlower (std::string &s)
    //{
    //    //std::transform (s.begin (), s.end (), std::back_inserter (s), ::tolower);
    //    std::transform (s.begin (), s.end (), s.begin (), ::tolower);
    //    return s;
    //}
    //inline std::string& strupper (std::string &s)
    //{
    //    //std::transform (s.begin (), s.end (), std::back_inserter (s), ::toupper);
    //    std::transform (s.begin (), s.end (), s.begin (), ::toupper);
    //    return s;
    //}

    //inline char toggle_c (char c)
    //{
    //    return char (islower (c) ? toupper (c) : tolower (c));
    //}

    inline std::string& toggle (std::string &s)
    {
        //transform (s.begin (), s.end (), s.begin (), toggle_c);

        transform (s.begin (), s.end (), s.begin (), [] (char c)->char
        {
            return char (islower (c) ? toupper (c) : tolower (c));
        });

        return s;
    }

    // char case-sensitive equals
    //inline bool equals_c (char c1, char c2) { return (c1 == c2); }

    // string case-sensitive equals
    inline bool  equals (const std::string &s1, const std::string &s2)
    {
        //return !strcmp(s1.c_str (), s2.c_str ());

        return (s1 == s2);
    }
    // string case-insensitive equals
    inline bool iequals (const std::string &s1, const std::string &s2)
    {
        //strlower (const_cast<string&> (s1)); //strupper ();
        //strlower (const_cast<string&> (s2)); //strupper ();
        //return (s1 == s2);

        //return !stricmp (s1.c_str (), s2.c_str ());

        //return s1.size () == s2.size ()
        //    && std::equal (s1.begin (), s1.end (), s2.begin (), equals_c);

        return s1.size () == s2.size ()
            && std::equal (s1.begin (), s1.end (), s2.begin (), [] (char c1, char c2)->bool
        {
            return toupper (c1) == toupper (c2);
        });

    }

    // trim from head
    inline std::string& ltrim (std::string &s, char c = ' ')
    {
        //s.erase (s.begin (),
        //    std::find_if (s.begin (), s.end (),
        //    std::not1 (std::bind2nd (std::ptr_fun<char, char, bool> (equals_c), c))));

        s.erase (s.begin (), std::find_if (s.begin (), s.end (), [&] (char ch)->bool { return (ch != c); }));

        return s;
    }
    // trim from tail
    inline std::string& rtrim (std::string &s, char c = ' ')
    {
        //s.erase (std::find_if (s.rbegin (), s.rend (),
        //    std::not1 (std::bind2nd (std::ptr_fun<char, char, bool> (equals_c), c))).base (),
        //    s.end ());

        s.erase (std::find_if (s.rbegin (), s.rend (), [&] (char ch)->bool { return (ch != c); }).base (), s.end ());

        return s;
    }
    // trim from both ends
    inline std::string&  trim (std::string &s, char c = ' ')
    {
        size_t length = s.length ();
        if (0 == length) return s;

        //// Only trim white space
        //std::stringstream trimmer;
        //trimmer << s;
        //s.clear();
        //trimmer >> s;

        //size_t p1 = s.find_first_not_of (c);
        //p1 = (std::string::npos == p1) ? 0 : p1;
        //size_t p2 = s.find_last_not_of (c);
        //p2 = (std::string::npos == p2) ? p1 : p2 - p1 + 1;
        //s = s.substr (p1, p2);

        //size_t p = s.find_last_not_of (c);
        //if (std::string::npos == p)
        //{
        //    s.clear ();
        //}
        //else
        //{
        //    if (length - 1 != p) s.erase (p + 1);
        //    p = s.find_first_not_of (c);
        //    if (0 < p)
        //        s.erase (0, p);
        //}

        //s.erase (
        //    std::find_if (s.rbegin (), s.rend (), [&] (char ch) { return (ch != c); }).base (),
        //    std::find_if (s.begin (), s.end (), [&] (char ch) { return (ch != c); }));

        ltrim (rtrim (s, c), c);

        return s;
    }

    inline std::string& reverse (std::string &s)
    {
        //std::stack<char> stk;
        //// Push characters from s onto the stack
        //for (size_t i=0; i < s.length (); ++i)
        //{
        //    stk.push (s[i]);
        //}
        //// Pop characters off of stack and put them back into s
        //for (size_t i=0; !stk.empty (); ++i, stk.pop ())
        //{
        //    s[i] = stk.top ();
        //}

        std::reverse (s.begin (), s.end ());
        return s;
    }

    template<class Pred>
    inline bool check_if (std::string &s, Pred &pred)
    {
        return (std::count_if (s.begin (), s.end (), pred) == s.length ());
    }
    template<class Pred>
    inline std::string& remove_if (std::string &s, Pred &pred)
    {
        s.erase (std::remove_if (s.begin (), s.end (), pred), s.end ());
        return s;
    }

    inline std::string& remove_substr (std::string &s, const std::string &sub)
    {
        const size_t length = sub.length ();
        if (0 < length)
        {
            size_t pos = s.find (sub);
            while (std::string::npos != pos)
            {
                s.erase (pos, length); // (start_position, number_of_symbols)
                pos = s.find (sub, pos);
            }
        }
        return s;
    }

    //inline void replace (std::string &s, const std::string &search, const std::string &replace)
    //{
    //    for (size_t pos = 0; ; pos += replace.length())
    //    {
    //        // Locate the substring to replace
    //        pos = s.find (search, pos);
    //        if (std::string::npos == pos) break;
    //        // Replace by erasing and inserting
    //        s.erase (pos, search.length());
    //        s.insert (pos, replace);
    //    }
    //}

    //inline std::string remove_dup (const std::string &s)
    //{
    //    // unique char set
    //    std::unordered_set<char> char_set (begin (s), end (s));
    //    std::string unique_str (begin (char_set), end (char_set));
    //    return unique_str;
    //}

    inline std::size_t count_substr (const std::string &s, const std::string &sub, bool overlap = true)
    {
        size_t count = 0;
        const size_t length = sub.length ();
        if (0 < length)
        {
            size_t pos = s.find (sub);
            while (std::string::npos != pos)
            {
                ++count;
                pos = s.find (sub, pos + (overlap ? 1 : length));
            }
        }
        return count;
    }

    inline std::vector<std::string> strsplit (const std::string &s, char delim = ' ', bool keep_empty = false, bool trim_entry = false)
    {
        std::vector<std::string> split;

        //std::istringstream iss (s);
        //std::string part;
        //bool success = false;
        //do
        //{
        //    part.clear ();
        //    success = !std::getline (iss, part, delim).fail ();
        //    if (trim_entry)
        //    {
        //        part = trim (part);
        //    }
        //    if (keep_empty || !empty (part))
        //    {
        //        split.push_back (part);
        //    }
        //}
        //while (success && iss.good ());

        //std::string::const_iterator cbeg = s.begin ();
        //std::string::const_iterator end = s.end ();
        //while (cbeg <= end)
        //{
        //    std::string::const_iterator cmid = find (cbeg, end, delim); // find_if(cbeg, end, isspace);
        //    std::string part = string (cbeg, cmid);
        //    if (trim_entry)
        //    {
        //        part = trim (part);
        //    }
        //    if (keep_empty || !empty (part))
        //    {
        //        split.push_back (part);
        //    }
        //    if (cmid == end) break;
        //    cbeg = cmid + 1;
        //}

        //std::string dup = s;
        //while (0 <= dup.length ())
        //{
        //    size_t p0 = (keep_empty ? 0 : dup.find_first_not_of (delim));
        //    if (string::npos == p0 || p0 > dup.length ()) break;
        //    size_t p1 = dup.find_first_of (delim, p0);
        //    std::string part = dup.substr (p0, ((std::string::npos != p1) ? p1 : dup.length ()) - p0);
        //    if (trim_entry)
        //    {
        //        part = trim (part);
        //    }
        //    if (keep_empty || !empty (part))
        //    {
        //        split.push_back (part);
        //    }
        //    if (std::string::npos == p1) break;
        //    dup = dup.substr (p1 + 1);
        //}

        const size_t length = s.length ();
        size_t p1 = 0;
        while (p1 <= length)
        {
            size_t p0 = (keep_empty ? p1 : s.find_first_not_of (delim, p1));
            if (std::string::npos == p0 || p0 > length) break;
            p1 = s.find_first_of (delim, p0);
            std::string part = s.substr (p0, ((std::string::npos != p1) ? p1 : length) - p0);
            if (trim_entry)
            {
                part = trim (part);
            }
            if (keep_empty || !part.empty ())
            {
                split.push_back (part);
            }
            if (std::string::npos == p1) break;
            ++p1;
        }

        return split;
    }

    inline std::vector<std::string> strsplit (const std::string &s, const std::string &delim)
    {
        std::vector<std::string> split;
        std::string::size_type beg = 0;
        std::string::size_type end = 0;
        while (end != std::string::npos)
        {
            end = s.find (delim, beg);
            if (beg < s.size () && beg != end)
            {
                split.push_back (s.substr (beg, end - beg));
            }
            beg = end + delim.size ();
        }
        return split;
    }
    
    template <class T>
    // If we have a space as delimiter, we can split string more efficient way and even make use of templates too
    inline std::vector<T> strsplit (const std::string &s)
    {
        std::vector<T> split;
        std::istringstream iss (s);
        copy (std::istream_iterator<T> (ss), std::istream_iterator<T> (), back_inserter (split));
        return split;
    }

    template <class T>
    inline std::string vecjoin (const std::vector<T> &v, const std::string &delim)
    {
        std::ostringstream join;
        for (typename std::vector<T>::const_iterator itr = v.begin (); itr != v.end (); ++itr)
        {
            if (itr != v.begin ()) join << delim;
            join << *itr;
        }
        return join.str ();
    }

    //inline i32 to_int (const std::string &s)
    //{
    //    //return std::stoi (s, NULL, 10);
    //    return atoi (s.c_str() );
    //}

    //inline size_t find_sep_fn (const std::string &path)
    //{
    //    //size_t pos = path.find_last_of('/');
    //    //if (std::string::npos == pos) pos = path.find_last_of('\\');
    //    //return pos;
    //
    //    //static std::string const sep_fn ( "\\/" );
    //    //std::string::const_iterator pivot
    //    //    = std::find_first_of (path.rbegin (), path.rend (), sep_fn.rbegin (), sep_fn.rend ());
    //
    //    return path.find_last_of ("/\\");
    //
    //}

}

#endif // _XSTRING_H_INC_
