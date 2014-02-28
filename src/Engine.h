#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _ENGINE_H_
#define _ENGINE_H_

#include <string>

#include "Type.h"

namespace Engine {

    extern std::string info (bool uci = true);

    extern void run (const std::string &args);

    extern void exit (int32_t code);

}

#endif // _ENGINE_H_