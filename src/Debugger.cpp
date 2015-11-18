#include "Debugger.h"
#include "Thread.h"

namespace Debugger {

    using namespace std;

    namespace {

        u64 Hits[2] = { 0, 0 };
        i64 Mean[2] = { 0, 0 };
    }

    void dbg_hits_on (bool b, bool c)
    {
        static Mutex mutex;
        mutex.lock ();
        if (c) { ++Hits[0]; if (b) { ++Hits[1]; } }
        mutex.unlock ();
    }

    void dbg_mean_of (i64 v)
    {
        static Mutex mutex;
        mutex.lock ();
        ++Mean[0]; Mean[1] += v;
        mutex.unlock ();
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
