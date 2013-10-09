//#pragma once
#ifndef XSTRING_H_
#define XSTRING_H_

#include <cctype>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <unordered_set>
//#include <sstream>  // stringstream in trim
//#include <stack>    // stack<> in reverse

namespace std {

    inline bool whitespace (const std::string &s)
    {
        if (s.empty ()) return true;
        for (size_t i = 0; i < s.length (); ++i)
        {
            if (!isspace (unsigned char (s[i]))) return false;
        }
        return true;
    }

    inline std::string& to_lower (std::string &s)
    {
        std::transform (s.cbegin (), s.cend (), s.begin (), tolower);
        return s;
    }
    inline std::string& to_upper (std::string &s)
    {
        std::transform (s.cbegin (), s.cend (), s.begin (), toupper);
        return s;
    }

    // string case-sensitive equals
    inline bool  equals (const std::string &s1, const std::string &s2)
    {
        //return !strcmp(s1.c_str(), s2.c_str());
        return (s1 == s2);
    }
    // string case-insensitive equals
    inline bool iequals (const std::string &s1, const std::string &s2)
    {
        //std::string ss1 = s1;
        //std::string ss2 = s2;
        //to_lower (ss1); //to_upper (ss1);
        //to_lower (ss2); //to_upper (ss2);
        //return (ss1 == ss2);

        //return !stricmp(s1.c_str(), s2.c_str());

        return (s1.size () == s2.size ()) &&
            std::equal (s1.cbegin(), s1.cend(), s2.cbegin(), [] (char c1, char c2)
        {
            return toupper (c1) == toupper (c2);
        });
    }

    //// char case-sensitive equals
    //inline bool equals (int c1, int c2)
    //{
    //    return (c1 == c2);
    //}

    // trim from head
    inline std::string& ltrim (std::string &s, char c = ' ')
    {
        //s.erase (s.cbegin (),
        //    std::find_if (s.cbegin (), s.cend (),
        //    std::not1 (std::bind2nd (std::ptr_fun<int, int, bool> (equals), c))));

        s.erase (s.cbegin (), std::find_if (s.cbegin (), s.cend (), [&] (char ch) { return (ch != c); }));

        return s;
    }
    // trim from tail
    inline std::string& rtrim (std::string &s, char c = ' ')
    {
        //s.erase (std::find_if (s.crbegin (), s.crend (),
        //    std::not1 (std::bind2nd (std::ptr_fun<int, int, bool> (equals), c))).base (),
        //    s.cend ());

        s.erase (std::find_if (s.crbegin (), s.crend (), [&] (char ch) { return (ch != c); }).base (), s.cend ());

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
        return (std::count_if (s.cbegin (), s.cend (), pred) == s.length ());
    }
    template<class Pred>
    inline std::string& remove_if (std::string &s, Pred &pred)
    {
        s.erase (std::remove_if (s.begin (), s.end (), pred), s.cend ());
        return s;
    }

    inline std::string& remove_substring (std::string &s, const std::string &sub)
    {
        auto l = sub.length ();
        const size_t length = sub.length ();
        if (0 < length)
        {
            size_t pos = s.find (sub);
            while (std::string::npos != pos)
            {
                // erase (start_position_to_erase, number_of_symbols)
                s.erase (pos, length);
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

    inline std::string remove_dup (const std::string &s)
    {
        // unique char set
        std::unordered_set<char> set_chars (begin (s), end (s));
        std::string str_unique (begin (set_chars), end (set_chars));
        return str_unique;
    }

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

    inline std::vector<std::string> str_splits (const std::string &s, char delim = ' ', bool keep_empty = false, bool do_trim = false)
    {
        std::vector<std::string> list_s;

        //std::istringstream iss (s);
        //std::string part;
        //bool success = false;
        //do
        //{
        //    part.clear ();
        //    success = !std::getline (iss, part, delim).fail ();
        //    if (do_trim)
        //    {
        //        part = trim (part);
        //    }
        //    if (keep_empty || !empty (part))
        //    {
        //        list_s.emplace_back (part);
        //    }
        //}
        //while (success && iss.good ());

        //std::string::const_iterator cbeg = s.cbegin ();
        //std::string::const_iterator cend = s.cend ();
        //while (cbeg <= cend)
        //{
        //    std::string::const_iterator cmid = find (cbeg, cend, delim); // find_if(cbeg, cend, isspace);
        //    std::string part = string (cbeg, cmid);
        //    if (do_trim)
        //    {
        //        part = trim (part);
        //    }
        //    if (keep_empty || !empty (part))
        //    {
        //        list_s.emplace_back (part);
        //    }
        //    if (cmid == cend) break;
        //    cbeg = cmid + 1;
        //}

        //std::string dup = s;
        //while (0 <= dup.length ())
        //{
        //    size_t p0 = (keep_empty ? 0 : dup.find_first_not_of (delim));
        //    if (string::npos == p0 || p0 > dup.length ()) break;
        //    size_t p1 = dup.find_first_of (delim, p0);
        //    std::string part = dup.substr (p0, ((std::string::npos != p1) ? p1 : dup.length ()) - p0);
        //    if (do_trim)
        //    {
        //        part = trim (part);
        //    }
        //    if (keep_empty || !empty (part))
        //    {
        //        list_s.emplace_back (part);
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
            if (do_trim)
            {
                part = trim (part);
            }
            if (keep_empty || !part.empty ())
            {
                list_s.emplace_back (part);
            }
            if (std::string::npos == p1) break;
            ++p1;
        }

        return list_s;
    }

    //inline int to_int (const std::string &s)
    //{
    //    return std::stoi (s, NULL, 10);
    //}

    //inline size_t find_sep_fn (const std::string &path)
    //{
    //    //size_t pos = path.find_last_of('/');
    //    //if (std::string::npos == pos) pos = path.find_last_of('\\');
    //    //return pos;
    //
    //    //static std::string const sep_fn ( "\\/" );
    //    //std::string::const_iterator pivot
    //    //    = std::find_first_of (path.crbegin (), path.crend (), sep_fn.crbegin (), sep_fn.crend ());
    //
    //    return path.find_last_of ("/\\");
    //
    //}

}

#endif
