#include "Debugger.h"

namespace Debugger {

    using namespace std;

    namespace {

        u64 Hits[2];
        i64 Mean[2];
    }

    void dbg_hits_on (bool b, bool c)
    {
        if (c) { ++Hits[0]; if (b) ++Hits[1]; }
    }

    void dbg_mean_of (i64 v)
    {
        ++Mean[0]; Mean[1] += v;
    }

    void dbg_print ()
    {
        if (Hits[0] != 0)
        {
            std::cerr
                << "Total: " << setw (4) << Hits[0]
                << " Hits: " << setw (4) << Hits[1]
                << " Hit-rate (%): " << setw (4) << setprecision (2) << fixed << 100 * (double) Hits[1] / Hits[0]
                << std::endl;
        }

        if (Mean[0] != 0)
        {
            std::cerr
                << "Total: " << setw (4) << Mean[0]
                << " Mean: " << setw (4) << setprecision (2) << fixed << (double) Mean[1] / Mean[0]
                << std::endl;
        }
    }

}
