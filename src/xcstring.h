#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _XCSTRING_H_INC_
#define _XCSTRING_H_INC_

#include <cstring>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include "Platform.h"

#pragma warning (disable: 4996) // Function strcpy () may be unsafe use strcpy_s ()

inline bool null (const char *s)
{
    return (s == NULL);
}
inline bool empty (const char *s)
{
    assert (s);
    if (s == NULL) return true;
    return (*s == '\0');
}
inline bool whitespace (const char *s)
{
    assert (s);
    if (s == NULL) return true;
    while (*s != '\0')
    {
        if (!isspace (i32 (*s))) return false;
        ++s;
    }
    return true;
}

inline char* strlower (char *s)
{
    assert (s);
    if (s == NULL) return NULL;
    while (*s != '\0') { *s = char (tolower (i32 (*s))); ++s; }
    return s;
}
inline char* strupper (char *s)
{
    assert (s);
    if (s == NULL) return NULL;
    while (*s != '\0') { *s = char (toupper (i32 (*s))); ++s; }
    return s;
}

inline bool  equals (const char *s1, const char *s2, size_t n)
{
    assert (s1 && s2);
    return !strncmp (s1, s2, n);
}
inline bool  equals (const char *s1, const char *s2)
{
    assert (s1 && s2);
    return !strcmp (s1, s2);
}

inline bool iequals (const char *s1, const char *s2, size_t n)
{
    assert (s1 && s2);
    return !strnicmp (s1, s2, n);
}
inline bool iequals (const char *s1, const char *s2)
{
    assert (s1 && s2);
    return !stricmp (s1, s2);
}

// Insert char 'c' at position 'pos' [0...n] 
inline void insert_at (char *s, size_t pos, char c)
{
    assert (s);
    if (!s)     return;

    //s += pos;
    //while (c)
    //{
    //    swap (*s, c);
    //    ++s;
    //}
    //*s = '\0';

    size_t length = strlen (s);

    //s[length+1] = '\0';
    //for (size_t i = length; i > pos; --i)
    //{
    //    s[i] = s[i - 1];
    //}
    //s[pos] = c;
    
    // (s + make_room_at + room_to_make, s + make_room_at, size - (make_room_at + room_to_make) + 1)
    memmove (s + pos + 1, s + pos, length - pos);
    s[pos] = c;

}
// Remove char at position 'pos' [0...n-1]
inline void remove_at (char *s, size_t pos)
{
    assert (s);
    if (!s)     return;

    //s += pos;
    //while (*s)
    //{
    //    *s = *(s+1);
    //    ++s;
    //}

    size_t length = strlen (s);

    //for (size_t i = pos; i < length; ++i)
    //{
    //    s[i] = s[i + 1];
    //}
    //// --- s[length - 1] = '\0';

    memmove (s + pos, s + pos + 1, length - pos);
}

inline char* remove (char *s, char c = ' ')
{
    assert (s);
    if (!s) return NULL;

    //char *p = s;
    //while (*p && c != *p) ++p;
    //strcpy (p, p+1);

    char *p = strchr (s, c);
    if (p)
    {
        strcpy (p, p+1);
    }

    return s;
}

// Purge all char 'c'
inline char* remove_all (char *s, char c = ' ')
{
    assert (s);
    if (!s)     return NULL;

    //char *p = s;
    //while (*p)
    //{
    //    //if (c == *p)
    //    //{
    //    //    strcpy (p, p+1);
    //    //    continue;
    //    //}
    //    //++p;
    //    // ----------------------
    //    while (*p && c != *p) ++p;
    //    char *q = p;
    //    while (*q && c == *q) ++q;
    //    strcpy (p, q);
    //}

    char *p = strchr (s, c);
    while (p)
    {
        char *q = p;
        while (*q && c == *q) ++q;
        strcpy (p, q);
        p = strchr (p, c);
    }

    return s;
}

// Remove (first occurence of) sub
inline char* remove_substr (char *s, const char sub[])
{
    const size_t length = strlen (sub);
    char *p = strstr (s, sub);
    while (p)
    {
        strcpy (p, p + length);
        //memmove (p, p + length, strlen (p + length) + 1);
        p = strstr (p, sub);
    }
    return s;
}

// Remove duplicate characters
inline char* remove_dup (char *s)
{
    assert (s);
    if (!s)     return NULL;

    bool included[(1) << CHAR_BIT] = { false };
    size_t repeat  = 0;
    char const *p_read  = s;
    char       *p_write = s;
    while (*p_read)
    {
        if (included[*p_read])
        {
            ++repeat;
        }
        else
        {
            included[*p_read] = true;
            *p_write = *p_read;
            ++p_write;
        }
        ++p_read;
    }
    *p_write = '\0';
    return s;
}

inline char* ltrim (char *s, char c = ' ')
{
    assert (s);
    if (!s) return NULL;

    size_t length = strlen (s);
    if (0 < length)
    {

        //char cc[] = { c, '\0' };
        //size_t span = strspn (s, cc);
        //if (0 != span) strcpy (s, s + span);

        const char *p = s;
        while (*p && c == *p) ++p;
        if (p != s) strcpy (s, p);

    }
    return s;
}
inline char* rtrim (char *s, char c = ' ')
{
    assert (s);
    if (!s) return NULL;

    size_t length = strlen (s);
    if (0 == length) return s;
    char *p = s + length - 1;
    while (p >= s)
    {
        if (c != *p) break;
        *p = '\0';
        --p;
    }
    return s;
}
inline char*  trim (char *s, char c = ' ')
{
    assert (s);
    if (!s) return NULL;

    return ltrim (rtrim (s, c), c);
}

inline const char* find (const char *s, size_t n, char c)
{
    assert (s);
    if (!s)     return NULL;

    while (n > 0)
    {
        if (toupper (*s) == toupper (c)) break;
        ++s;
        --n;
    }
    return s;
}

inline char* substr (const char *s, size_t start = 0, size_t size = 1)
{
    assert (s);
    if (!s)     return NULL;

    size_t length = strlen (s);
    if (start > length) return NULL;
    if (size > (length - start)) return NULL;
    char *sub = (char*) malloc ((size + 1) * sizeof (char));
    if (sub)
    {
        strncpy (sub, s + start, size);
        sub[size] = '\0';
    }
    return sub;
}

inline size_t count_substr (const char *s, const char sub[], bool overlap = true)
{
    assert (s);
    if (!s) return 0;

    size_t count = 0;
    size_t length = strlen (sub);
    if (0 < length)
    {
        //while (*s)
        //{
        //    if (strncmp (s, sub, length))
        //    {
        //        ++s;
        //        continue;
        //    }
        //    ++count;
        //    s += (overlap ? 1 : length);
        //}

        const char *p = strstr (s, sub);
        while (p)
        {
            ++count;
            p = strstr (p + (overlap ? 1 : length), sub);
        }

    }
    return count;
}

inline char** strsplit (char *s, char delim = ' ', bool keep_empty = false, bool trim_entry = false, u32 *num_splits = NULL)
{
    assert (s);
    if (!s) return NULL;

    size_t length = strlen (s);
    char *p1 = s;
    u32 count = 0;
    while (p1 <= s + length)
    {
        char *p0 = p1;
        if (!keep_empty)
        {
            while (*p1 && delim == *p1) ++p1;
            if (empty(p1)) break;
            if (p0 != p1) p0 = p1;
        }
        ++count;
        p1 = strchr (p0, delim);
        if (!p1) break;
        ++p1;
    }

    char **list =
        (char**) malloc ((count+1) * sizeof (char *));
        //(char**) calloc ((count+1), sizeof (char *));

    if (!list) return NULL;

    u32 idx = 0;
    // --- have to free all list[0...n] and list, not works for keep_empty

    //const char delim_s[] = { delim, '\0' };
    //char *dup = strdup (s);
    //if (dup)
    //{
    //    char *token = strtok (dup, delim_s);
    //    while (token)
    //    {
    //        assert (idx <= count);
    //        char *part  = strdup (token);
    //        if (part)
    //        {
    //            if (trim_entry)
    //            {
    //                part = trim (part);
    //            }
    //            if (keep_empty || !empty (part))
    //            {
    //                list[idx++] = part;
    //            }
    //            else
    //            {
    //                free (part);
    //            }
    //        }
    //        token   = strtok (NULL, delim_s);
    //    }
    //    assert (idx == count);
    //    list[idx] = NULL;
    //    free (dup);
    //}

    // --------------------------------------

    // --- only have to free list[0] and list, works for keep_empty
    char *dup = strdup (s);
    p1 = dup;
    while (p1 <= dup + length)
    {
        char *p0 = p1;
        if (!keep_empty)
        {
            while (*p1 && delim == *p1) ++p1;
            if (empty(p1)) break;
            if (p0 != p1) strcpy (p0, p1);
        }
        p1 = strchr (p0, delim);
        if (p1) *p1 = '\0';
        if (trim_entry)
        {
            p0 = trim (p0);
        }
        if (keep_empty || !empty (p0))
        {
            list[idx++] = p0;
        }
        if (!p1) break;
        ++p1;
    }

    assert (idx == count);
    list[idx] = NULL;

    if (num_splits)
    {
        *num_splits  = count;
    }

    return list;
}

inline char* strjoin (char **ss, u32 count, char delim = ' ')
{
    size_t length = 0;
    u32 *sub_length = (u32 *) malloc ((count) * sizeof (u32));

    for (u32 i = 0; i < count; ++i)
    {
        sub_length[i] = strlen (ss[i]);
        length += sub_length[i];
    }

    char *join = 
        (char *) malloc ((length+count) * sizeof (char));
        //(char *) calloc ((length+count), sizeof (char));

    length = 0;
    for (u32 i = 0; i < count; ++i)
    {
        //memcpy (join+length+i, ss[i], sub_length[i]);
        strcpy (join+length+i, ss[i]);
        length += sub_length[i];
        join[length+i] = delim;
    }
    
    free (sub_length);
    join[length+count-1] = '\0';
    return join;
}


inline int   to_int (const char *s)
{
    assert (s);
    if (!s)     return 0;

    return atoi (s);
}
inline long  to_long (const char *s)
{
    assert (s);
    if (!s)     return 0L;

    //return atol (s);

    char *end;
    long l = strtol (s, &end, 10);
    assert (LONG_MIN > l && l < LONG_MAX);
    return l;
}
inline char* to_str (int i, char *s, int radix = 10)
{
    assert (s);
    if (!s)     return NULL;

    return itoa (i, s, radix);
}

//inline void erase (char *(&s))
//{
//    assert (s);
//    if (!s)   return;
//    free (s);
//    s = NULL;
//}

#endif // _XCSTRING_H_INC_
