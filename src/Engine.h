//#pragma once
#ifndef ENGINE_H_
#define ENGINE_H_

#include <string>
#include "Type.h"

namespace Engine {

    extern ::std::string info (bool uci = false);

    extern void start ();

    extern void stop ();

    extern void exit (int32_t code);

}

#endif