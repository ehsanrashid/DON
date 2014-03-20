#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _TB_SYZYGY_H_INC
#define _TB_SYZYGY_H_INC

#include "Type.h"

class Position;

namespace TBSyzygy {

    extern i32 TB_Largest;
    
    extern i32 probe_wdl   (Position &pos, i32 *success);
    extern i32 probe_dtz   (Position &pos, i32 *success);
    
    extern bool root_probe     (Position &pos, Value &TBScore);
    extern bool root_probe_wdl (Position &pos, Value &TBScore);

    extern void initialize     (std::string &path);

}

#endif // _TB_SYZYGY_H_INC
