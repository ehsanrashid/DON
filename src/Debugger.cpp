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
            std::cerr << right
                << "Total :" << setw (20) << Hits[0] << "\n"
                << "Hits  :" << setw (20) << Hits[1] << "\n"
                << "Rate  :" << setw (20) << setprecision (2) << fixed << 100 * (double) Hits[1] / Hits[0]
                << left << std::endl;
        }

        if (Mean[0] != 0)
        {
            std::cerr << right
                << "Total :" << setw (20) << Mean[0] << "\n"
                << "Mean  :" << setw (20) << setprecision (2) << fixed << (double) Mean[1] / Mean[0]
                << left << std::endl;
        }
    }

}
