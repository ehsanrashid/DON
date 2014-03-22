/*
Base64.h & Base64.cpp

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

#include "Base64.h"

using namespace std;

namespace {

    const string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    inline bool is_base64 (u08 c)
    {
        return (c != '=') && (isalnum (c) || (c == '+') || (c == '/'));
    }

}

void encode_base64 (const u08 *decoded, u32 dec_len, u08 *encoded, u32 &enc_len)
{
    u08 i = 0;
    u08 buff_3[3];
    u08 buff_4[4];
    
    enc_len = 0;
    while (dec_len--)
    {
        buff_3[i++] = *(decoded++);
        if (i == 3)
        {
            buff_4[0] = ((buff_3[0] & 0xFC) >> 2);
            buff_4[1] = ((buff_3[0] & 0x03) << 4) + ((buff_3[1] & 0xF0) >> 4);
            buff_4[2] = ((buff_3[1] & 0x0F) << 2) + ((buff_3[2] & 0xC0) >> 6);
            buff_4[3] = ((buff_3[2] & 0x3F) << 0);

            for (i = 0; i < 4; ++i)
            {
                encoded[enc_len++] = base64_chars[buff_4[i]];
            }
            i = 0;
        }
    }

    if (i > 0)
    {
        u08 j;
        for (j = i; j < 3; ++j)
        {
            buff_3[j] = '\0';
        }

        buff_4[0] = ((buff_3[0] & 0xFC) >> 2);
        buff_4[1] = ((buff_3[0] & 0x03) << 4) + ((buff_3[1] & 0xF0) >> 4);
        buff_4[2] = ((buff_3[1] & 0x0F) << 2) + ((buff_3[2] & 0xC0) >> 6);
        buff_4[3] = ((buff_3[2] & 0x3F) << 0);

        for (j = 0; j < i + 1; j++)
        {
            encoded[enc_len++] = base64_chars[buff_4[j]];
        }
        while (i++ < 3)
        {
            encoded[enc_len++] = '=';
        }
    }
}

void decode_base64 (const u08 *encoded, u32 enc_len, u08 *decoded, u32 &dec_len)
{
    u08 i = 0;
    u08 buff_4[4];
    u08 buff_3[3];
    
    dec_len = 0;
    while (enc_len-- && is_base64 (*encoded))
    {
        buff_4[i++] = *(encoded++);

        if (i == 4)
        {
            for (i = 0; i < 4; ++i)
            {
                buff_4[i] = base64_chars.find (buff_4[i]);
            }
            buff_3[0] = ((buff_4[0] & 0xFF) << 2) + ((buff_4[1] & 0x30) >> 4);
            buff_3[1] = ((buff_4[1] & 0x0F) << 4) + ((buff_4[2] & 0x3C) >> 2);
            buff_3[2] = ((buff_4[2] & 0x03) << 6) + ((buff_4[3] & 0xFF) >> 0);

            for (i = 0; i < 3; ++i)
            {
                decoded[dec_len++] = buff_3[i];
            }
            i = 0;
        }
    }

    if (i > 0)
    {
        u08 j;
        for (j = i; j < 4; ++j)
        {
            buff_4[j] = 0;
        }
        for (j = 0; j < 4; ++j)
        {
            buff_4[j] = base64_chars.find (buff_4[j]);
        }
        buff_3[0] = ((buff_4[0] & 0xFF) << 2) + ((buff_4[1] & 0x30) >> 4);
        buff_3[1] = ((buff_4[1] & 0x0F) << 4) + ((buff_4[2] & 0x3C) >> 2);
        buff_3[2] = ((buff_4[2] & 0x03) << 6) + ((buff_4[3] & 0xFF) >> 0);

        for (j = 0; j < i - 1; ++j)
        {
            decoded[dec_len++] = buff_3[j];
        }
    }
}


string encode_base64 (const string &decoded)
{
    u08 const* dec_bytes = reinterpret_cast<u08 const*> (decoded.c_str ());
    u32 dec_len = decoded.length ();

    u08 i = 0;
    u08 buff_3[3];
    u08 buff_4[4];

    string encoded;

    while (dec_len--)
    {
        buff_3[i++] = *(dec_bytes++);

        if (i == 3)
        {
            buff_4[0] = ((buff_3[0] & 0xFC) >> 2);
            buff_4[1] = ((buff_3[0] & 0x03) << 4) + ((buff_3[1] & 0xF0) >> 4);
            buff_4[2] = ((buff_3[1] & 0x0F) << 2) + ((buff_3[2] & 0xC0) >> 6);
            buff_4[3] = ((buff_3[2] & 0x3F) << 0);

            for (i = 0; i < 4; ++i)
            {
                encoded += base64_chars[buff_4[i]];
            }
            i = 0;
        }
    }

    if (i > 0)
    {
        u08 j;
        for (j = i; j < 3; ++j)
        {
            buff_3[j] = '\0';
        }

        buff_4[0] = ((buff_3[0] & 0xFC) >> 2);
        buff_4[1] = ((buff_3[0] & 0x03) << 4) + ((buff_3[1] & 0xF0) >> 4);
        buff_4[2] = ((buff_3[1] & 0x0F) << 2) + ((buff_3[2] & 0xC0) >> 6);
        buff_4[3] = ((buff_3[2] & 0x3F) << 0);

        for (j = 0; j < i + 1; j++)
        {
            encoded += base64_chars[buff_4[j]];
        }
        while (i++ < 3)
        {
            encoded += '=';
        }
    }

    return encoded;
}

string decode_base64 (const string &encoded)
{
    u08 const* enc_bytes = reinterpret_cast<u08 const*> (encoded.c_str ());
    u32 enc_len = encoded.length ();

    u08 i = 0;
    u08 buff_4[4];
    u08 buff_3[3];

    string decoded;

    while (enc_len-- && is_base64 (*enc_bytes))
    {
        buff_4[i++] = *(enc_bytes++);

        if (i == 4)
        {
            for (i = 0; i < 4; ++i)
            {
                buff_4[i] = base64_chars.find (buff_4[i]);
            }
            buff_3[0] = ((buff_4[0] & 0xFF) << 2) + ((buff_4[1] & 0x30) >> 4);
            buff_3[1] = ((buff_4[1] & 0x0F) << 4) + ((buff_4[2] & 0x3C) >> 2);
            buff_3[2] = ((buff_4[2] & 0x03) << 6) + ((buff_4[3] & 0xFF) >> 0);

            for (i = 0; i < 3; ++i)
            {
                decoded += buff_3[i];
            }
            i = 0;
        }
    }

    if (i > 0)
    {
        u08 j;
        for (j = i; j < 4; ++j)
        {
            buff_4[j] = 0;
        }
        for (j = 0; j < 4; ++j)
        {
            buff_4[j] = base64_chars.find (buff_4[j]);
        }
        buff_3[0] = ((buff_4[0] & 0xFF) << 2) + ((buff_4[1] & 0x30) >> 4);
        buff_3[1] = ((buff_4[1] & 0x0F) << 4) + ((buff_4[2] & 0x3C) >> 2);
        buff_3[2] = ((buff_4[2] & 0x03) << 6) + ((buff_4[3] & 0xFF) >> 0);

        for (j = 0; j < i - 1; ++j)
        {
            decoded += buff_3[j];
        }
    }

    return decoded;
}
