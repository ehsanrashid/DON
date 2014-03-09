#include "DebugLogger.h"

using namespace std;

namespace {

    uint64_t
        Hits [2] = { U64 (0), U64 (0), },
        Means[2] = { U64 (0), U64 (0), };
}

void dbg_hit_on (bool h, bool c)
{
    if (c) { ++Hits[0]; if (h) ++Hits[1]; }
}
void dbg_mean_of (uint64_t v)
{
    ++Means[0]; Means[1] += v;
}

void dbg_print ()
{
    if (Hits[0])
    {
        cerr
            << "Total " << (Hits[0])
            << " Hits " << (Hits[1])
            << " Hit-rate (%) " << (100 * Hits[1] / Hits[0])
            << endl;
    }
    if (Means[0])
    {
        cerr
            << "Total " << (Means[0])
            << " Mean " << double (Means[1]) / double (Means[0])
            << endl;
    }
}
