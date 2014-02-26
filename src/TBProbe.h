//#pragma once
#ifndef TB_PROBE_H_
#define TB_PROBE_H_

#include "Type.h"

class Position;

namespace Tablebases {

    extern int32_t TBLargest;

    void initialize     (const std::string &path);
    
    int32_t probe_wdl   (Position &pos, int32_t *success);
    int32_t probe_dtz   (Position &pos, int32_t *success);
    
    bool root_probe     (Position &pos, Value &TBScore);
    bool root_probe_wdl (Position &pos, Value &TBScore);

}

#endif
