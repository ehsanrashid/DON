#include "Debugger.h"
#include "Thread.h"

namespace Debugger {

    using namespace std;

    namespace {

        u64 CondCount   = 0;
        u64 HitCount    = 0;

        u64 ItemCount   = 0;
        i64 ItemSum     = 0;
    }

    void dbg_hit_on (bool hit)
    {
        static Mutex mutex;
        mutex.lock ();
        ++CondCount;
        if (hit)
        {
            ++HitCount;
        }
        mutex.unlock ();
    }

    void dbg_hit_on (bool cond, bool hit)
    {
        if (cond)
        {
            dbg_hit_on (hit);
        }
    }

    void dbg_mean_of (i64 item)
    {
        static Mutex mutex;
        mutex.lock ();
        ++ItemCount;
        ItemSum += item;
        mutex.unlock ();
    }

    void dbg_print ()
    {
        if (CondCount != 0)
        {
            std::cerr << right
                << "---------------------------\n"
                << "Cond  :" << setw (20) << CondCount << "\n"
                << "Hit   :" << setw (20) << HitCount  << "\n"
                << "Rate  :" << setw (20) << setprecision (2) << fixed << 100.0 * (double)HitCount / CondCount
                << left << std::endl;
        }

        if (ItemCount != 0)
        {
            std::cerr << right
                << "---------------------------\n"
                << "Count :" << setw (20) << ItemCount << "\n"
                << "Sum   :" << setw (20) << ItemSum   << "\n"
                << "Mean  :" << setw (20) << setprecision (2) << fixed << 1.0 * (double)ItemSum / ItemCount
                << left << std::endl;
        }
    }

}
