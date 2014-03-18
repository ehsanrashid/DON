#include "Debugger.h"
#include "Thread.h"

namespace Debugger {

    using namespace std;

    namespace {

        uint64_t
            Hits [2] = { U64 (0), U64 (0) },
            Means[2] = { U64 (0), U64 (0) };
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
            sync_cout
                << "Total: "  << setw (4) << (Hits[0])
                << ", Hits: " << setw (4) << (Hits[1])
                << ", Hit-rate (%): " << setw (4) << setprecision (2) << fixed
                << (100 * (double) Hits[1] / Hits[0])
                << sync_endl;
        }
        if (Means[0])
        {
            sync_cout
                << "Total: "  << setw (4) << (Means[0])
                << ", Mean: " << setw (4) << setprecision (2) << fixed
                << ((double) Means[1] / Means[0])
                << sync_endl;
        }
    }

}
