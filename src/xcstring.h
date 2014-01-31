//#pragma once
#ifndef XCSTRING_H_
#define XCSTRING_H_

#include <cstring>
#include <cassert>
#include <cctype>
#include <cstdlib>

//#pragma warning (disable: 4996) // Function strcpy () may be unsafe use strcpy_s ()

inline bool null (const char s[])
{
    return !s;
}
inline bool empty (const char s[])
{
    assert (s);
    if (!s)     return false;
    return !(*s);
}
inline bool whitespace (const char s[])
{
    assert (s);
    if (!s)     return true;
    while (*s)
    {
        if (!isspace (unsigned char (*s))) return false;
        ++s;
    }
    return true;
}

inline char* to_lower (char s[])
{
    assert (s);
    if (!s)     return NULL;
    while (*s) *s++ = char (tolower (*s));
    return s;
}
inline char* to_upper (char s[])
{
    assert (s);
    if (!s)     return NULL;
    while (*s) *s++ = char (toupper (*s));
    return s;
}

inline bool  equals (const char s1[], const char s2[], size_t n)
{
    assert (s1 && s2);
    return !strncmp (s1, s2, n);
}
inline bool  equals (const char s1[], const char s2[])
{
    assert (s1 && s2);
    return !strcmp (s1, s2);
}

inline bool iequals (const char s1[], const char s2[], size_t n)
{
    assert (s1 && s2);
    return !strnicmp (s1, s2, n);
}
inline bool iequals (const char s1[], const char s2[])
{
    assert (s1 && s2);
    return !stricmp (s1, s2);
}

// Insert char 'c' at position 'pos' [0...n] 
inline void insert_at (char s[], size_t pos, char c)
{
    assert (s);
    if (!s)     return;

    //s += pos;
    //while (c)
    //{
    //    std::swap (*s, c);
    //    ++s;
    //}
    //*s = '\0';

    size_t length = strlen (s);

    //s[length + 1] = '\0';
    //for (size_t i = length; i > pos; --i)
    //{
    //    s[i] = s[i - 1];
    //}
    //s[pos] = c;

    //memmove (s + make_room_at + room_to_make, s + make_room_at, size - (make_room_at + room_to_make) + 1);
    memmove (s + pos + 1, s + pos, length - pos);
    s[pos] = c;

}
// Remove char at position 'pos' [0...n-1]
inline void remove_at (char s[], size_t pos)
{
    assert (s);
    if (!s)     return;

    //s += pos;
    //while (*s)
    //{
    //    *s = *(s + 1);
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


inline char* remove (char s[], char c = ' ')
{
    assert (s);
    if (!s)     return NULL;

    //char const *p_read  = s;
    //char       *p_write = s;
    //while (*p_read)
    //{
    //    if (c != *p_read)
    //    {
    //        if (p_write != p_read)
    //        {
    //            *p_write = *p_read;
    //        }
    //        ++p_write;
    //    }
    //    else
    //    {
    //        // write all character after 1st miss-match
    //        while (*++p_read)
    //        {
    //            *p_write = *p_read;
    //            ++p_write;
    //        }
    //        break;
    //    }
    //    ++p_read;
    //}
    //*p_write = '\0';
    //return s;

    char *p = strchr (s, c);
    if (p)  strcpy (p, p + 1);
    return s;
}
// Purge all char 'c'
inline char* remove_all (char s[], char c = ' ')
{
    assert (s);
    if (!s)     return NULL;

    //char *p = remove (s, s + strlen (s), c);
    //*p = '\0';
    //return s;

    //char const *p_read  = s;
    //char       *p_write = s;
    //while (*p_read)
    //{
    //    if (c != *p_read)
    //    {
    //        if (p_write != p_read)
    //        {
    //            *p_write = *p_read;
    //        }
    //        ++p_write;
    //    }
    //    ++p_read;
    //}
    //*p_write = '\0';
    //return s;

    char *p = strchr (s, c);
    while (p)
    {
        //strcpy (p, p + 1);
        memmove (p, p + 1, strlen (p + 1) + 1);
        p = strchr (p, c);
    }
    return s;

}

// Remove (first occurence of) sub
inline char* remove_substring (char s[], const char sub[])
{
    const size_t length = strlen (sub);
    char *p = strstr (s, sub);
    while (p)
    {
        //strcpy (p, p + length);
        memmove (p, p + length, strlen (p + length) + 1);
        p = strstr (p , sub);
    }
    return s;
}

// Remove duplicate characters
inline char* remove_dup (char s[])
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

inline char* ltrim (char s[], char c = ' ')
{
    assert (s);
    if (!s)     return NULL;

    const size_t length = strlen (s);
    if (0 < length)
    {

        //char cc[] = { c, '\0' };
        //size_t span = strspn (s, cc);
        //if (0 != span) strcpy (s, s + span);

        const char *p = s;
        while (*p)
        {
            if (c != *p) break;
            ++p;
        }
        if (p != s) strcpy (s, p);

    }
    return s;
}
inline char* rtrim (char s[], char c = ' ')
{
    assert (s);
    if (!s)     return NULL;

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
inline char*  trim (char s[], char c = ' ')
{
    assert (s);
    if (!s)     return NULL;

    return ltrim (rtrim (s, c), c);
}

inline const char* find (const char s[], size_t n, char c)
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

inline char* substr (const char s[], size_t start = 0, size_t size = 1)
{
    assert (s);
    if (!s)     return NULL;

    size_t length = strlen (s);
    if (start > length) return NULL;
    if (size > (length - start)) return NULL;
    char *sub = (char*) malloc ((size + 1) * sizeof (*s));
    if (sub)
    {
        strncpy (sub, s + start, size);
        sub[size] = '\0';
    }
    return sub;
}

inline size_t count_substr (const char s[], const char sub[], bool overlap = true)
{
    assert (s);
    if (!s)     return 0;
    
    size_t count = 0;
    const size_t length = strlen (sub);
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

inline char** str_splits (char s[], char delim = ' ', bool keep_empty = false, bool trim_entry = false, intptr_t *num_splits = NULL)
{
    assert (s);
    if (!s)     return NULL;

    char **list = NULL;
    size_t count = 0;

    if (s)
    {
        char *p;
        char *l; // last delim

        p = strchr (s, delim);
        l = NULL;
        while (p)
        {
            ++count;
            l = p;
            ++p;
            if (!keep_empty)
            {
                while (*p && delim == *p++) l = p;
            }
            p = strchr (p, delim);
        }
        // Add space for trailing token.
        count += (l <= (s + strlen (s) - 1));

        list =
            (char**) malloc ((count + 1) * sizeof (*list));
        //(char**) calloc ((count + 1), sizeof (*list));
        if (list)
        {
            size_t idx = 0;

            // --- have to free all list[0...n] and list, not works for keep_empty

            //const char delim_s[] = { delim, '\0' };
            //char *dup   = _strdup (s);
            //char *token = strtok (dup, delim_s);
            //while (token)
            //{
            //    //ASSERT (idx <= count);
            //    char *part  = strdup (token);
            //    if (part)
            //    {
            //        if (trim_entry)
            //        {
            //            part = trim (part);
            //        }

            //        if (keep_empty || !empty (part))
            //        {
            //            list[idx++] = part;
            //        }
            //        else
            //        {
            //            free (part);
            //        }
            //    }
            //    token   = strtok (NULL, delim_s);
            //}

            ////ASSERT (idx == count);
            //list[idx] = NULL;
            //if (dup) free (dup);

            // --------------------------------------

            // --- only have to free list[0] and list, works for keep_empty

            p = _strdup (s);
            list[idx++] = p;

            p = strchr (p, delim);
            while (p)
            {
                *p++ = '\0';
                if (!keep_empty)
                {
                    while (*p && delim == *p) ++p;
                }

                //ASSERT (idx <= count);
                char *part  = p;
                if (part)
                {
                    if (trim_entry)
                    {
                        part = trim (part);
                    }

                    if (keep_empty || !empty (part))
                    {
                        if (idx < count) list[idx++] = part;
                    }
                }
                p = strchr (p, delim);
            }
            //ASSERT (idx == count);
            if (idx == count) list[idx] = NULL;
        }
    }

    if (num_splits)
    {
        *num_splits  = count;
    }

    return list;
}

inline int   to_int (const char s[])
{
    assert (s);
    if (!s)     return 0;

    return atoi (s);
}
inline long  to_long (const char s[])
{
    assert (s);
    if (!s)     return 0L;
    
    //return atol (s);
    
    char *end;
    long l = strtol (s, &end, 10);
    assert (LONG_MIN > l && l < LONG_MAX);
    return l;
}
inline char* to_str (int i, char s[], int radix = 10)
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

#endif
