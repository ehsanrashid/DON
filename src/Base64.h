#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _BASE_64_INC_
#define _BASE_64_INC_

#include <string>

extern std::string encode_base64 (const std::string &decoded_string);
extern std::string decode_base64 (const std::string &encoded_string);

#endif
