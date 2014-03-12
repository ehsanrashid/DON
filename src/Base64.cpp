/*
base64.cpp and base64.h

Copyright (C) 2004-2008 René Nyffenegger

This source code is provided 'as-is', without any express or implied
warranty. In no event will the author be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this source code must not be misrepresented; you must not
claim that you wrote the original source code. If you use this source code
in a product, an acknowledgment in the product documentation would be
appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not be
misrepresented as being the original source code.

3. This notice may not be removed or altered from any source distribution.

René Nyffenegger rene.nyffenegger@adp-gmbh.ch

*/

#include "base64.h"

#include <iostream>

using namespace std;

static const string base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";


static inline bool is_base64 (unsigned char c)
{
    return (c != '=') && (isalnum (c) || (c == '+') || (c == '/'));
}

string encode_base64 (const string &decoded_string)
{
    unsigned char const* encoded_bytes = reinterpret_cast<const unsigned char*>(decoded_string.c_str ());
    unsigned int encoded_len = decoded_string.length ();

    int i = 0;

    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    string encoded;

    while (encoded_len--)
    {
        char_array_3[i++] = *(encoded_bytes++);
        if (i == 3)
        {
            char_array_4[0] = ((char_array_3[0] & 0xFC) >> 2);
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xF0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0F) << 2) + ((char_array_3[2] & 0xC0) >> 6);
            char_array_4[3] = ((char_array_3[2] & 0x3F) << 0);

            for (i = 0; i < 4; ++i)
            {
                encoded += base64_chars[char_array_4[i]];
            }
            i = 0;
        }
    }

    if (i)
    {
        int j = 0;
        for (j = i; j < 3; ++j)
        {
            char_array_3[j] = '\0';
        }

        char_array_4[0] = ((char_array_3[0] & 0xFC) >> 2);
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xF0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0F) << 2) + ((char_array_3[2] & 0xC0) >> 6);
        char_array_4[3] = ((char_array_3[2] & 0x3F) << 0);

        for (j = 0; (j < i + 1); j++)
        {
            encoded += base64_chars[char_array_4[j]];
        }
        while ((i++ < 3))
        {
            encoded += '=';
        }
    }

    return encoded;

}

string decode_base64 (const string &encoded_string)
{
    unsigned char const* decoded_bytes = reinterpret_cast<const unsigned char*>(encoded_string.c_str ());
    int decoded_len = encoded_string.length ();

    int i = 0;

    unsigned char char_array_4[4];
    unsigned char char_array_3[3];

    string decoded;

    while (decoded_len-- && is_base64 (*decoded_bytes))
    {
        char_array_4[i++] = *(decoded_bytes++);

        if (i ==4)
        {
            for (i = 0; i < 4; ++i)
            {
                char_array_4[i] = base64_chars.find (char_array_4[i]);
            }
            char_array_3[0] = ((char_array_4[0] & 0xFF) << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0x0F) << 4) + ((char_array_4[2] & 0x3C) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x03) << 6) + ((char_array_4[3] & 0xFF) >> 0);

            for (i = 0; i < 3; ++i)
            {
                decoded += char_array_3[i];
            }
            i = 0;
        }
    }

    if (i)
    {
        int j = 0;
        for (j = i; j < 4; ++j)
        {
            char_array_4[j] = 0;
        }
        for (j = 0; j < 4; ++j)
        {
            char_array_4[j] = base64_chars.find (char_array_4[j]);
        }
        char_array_3[0] = ((char_array_4[0] & 0xFF) << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0x0F) << 4) + ((char_array_4[2] & 0x3C) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x03) << 6) + ((char_array_4[3] & 0xFF) >> 0);

        for (j = 0; j < i - 1; ++j)
        {
            decoded += char_array_3[j];
        }
    }

    return decoded;
}
