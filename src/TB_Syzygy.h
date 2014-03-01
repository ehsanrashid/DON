#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _TB_SYZYGY_H_INC
#define _TB_SYZYGY_H_INC

#include "Type.h"

class Position;

namespace TBSyzygy {

    extern int32_t TB_Largest;
    
    extern int32_t probe_wdl   (Position &pos, int32_t *success);
    extern int32_t probe_dtz   (Position &pos, int32_t *success);
    
    extern bool root_probe     (Position &pos, Value &TBScore);
    extern bool root_probe_wdl (Position &pos, Value &TBScore);

    extern void initialize     (const std::string &path);

}

#endif // _TB_SYZYGY_H_INC
