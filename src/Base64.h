#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _BASE_64_INC_
#define _BASE_64_INC_

#include <string>

#include "Platform.h"

extern void encode_base64 (const u08 *decoded, u32 dec_len, u08 *encoded, u32 &enc_len);
extern void decode_base64 (const u08 *encoded, u32 enc_len, u08 *decoded, u32 &dec_len);

extern std::string encode_base64 (const std::string &decoded);
extern std::string decode_base64 (const std::string &encoded);

#endif
