#include "Debugger.h"
#include "Thread.h"

namespace Debug {

    using namespace std;

    namespace {

        u64 Hits[2] = { U64(0), U64(0) },
            Mean[2] = { U64(0), U64(0) };
    }

    void dbg_hits_on (bool h, bool c)
    {
        if (c) { ++Hits[0]; if (h) ++Hits[1]; }
    }
    void dbg_mean_of (u64 v)
    {
        ++Mean[0]; Mean[1] += v;
    }

    void dbg_print ()
    {
        if (Hits[0])
        {
            sync_cout
                << "Total: "  << setw (4) << (Hits[0])
                << ", Hits: " << setw (4) << (Hits[1])
                << ", Hit-rate (%): " << setw (4) << setprecision (2) << fixed
                << (100.0f * float (Hits[1]) / float (Hits[0]))
                << sync_endl;
        }

        if (Mean[0])
        {
            sync_cout
                << "Total: "  << setw (4) << (Mean[0])
                << ", Mean: " << setw (4) << setprecision (2) << fixed
                << (float (Mean[1]) / float (Mean[0]))
                << sync_endl;
        }
    }

}
