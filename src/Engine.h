#ifndef _ENGINE_H_INC_
#define _ENGINE_H_INC_

#include "Type.h"

namespace Engine {

    extern std::string info (bool uci = true);

    extern void run (const std::string &arg);

    extern void exit (i32 code);

}

#endif // _ENGINE_H_INC_
