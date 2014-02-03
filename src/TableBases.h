//#pragma once
#ifndef TABLE_BASES_H_
#define TABLE_BASES_H_

#include "Position.h"

namespace Tablebases {

    extern bool initialized;

    void initialize     (const std::string& path);

    int probe_wdl       (Position &pos, int32_t *success);
    int probe_dtz       (Position &pos, int32_t *success);
    bool root_probe     (Position &pos);
    bool root_probe_wdl (Position &pos);

}

#endif