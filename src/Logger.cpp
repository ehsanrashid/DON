#include "Logger.h"

#include <atomic>
#include <iomanip>

Logger Log;

using namespace std;

namespace {

    atomic<u64> CondCount;
    atomic<u64> HitCount;

    atomic<u64> ItemCount;
    atomic<i64> ItemSum;
}

void initializeDebug()
{
    CondCount = 0;
    HitCount = 0;

    ItemCount = 0;
    ItemSum = 0;
}

void debugHit(bool hit)
{
    ++CondCount;
    if (hit)
    {
        ++HitCount;
    }
}

void debugHitOn(bool cond, bool hit)
{
    if (cond)
    {
        debugHit(hit);
    }
}

void debugMeanOf(i64 item)
{
    ++ItemCount;
    ItemSum += item;
}

void debugPrint()
{
    if (0 != CondCount)
    {
        cerr << right
             << "---------------------------\n"
             << "Cond  :" << setw(20) << CondCount << "\n"
             << "Hit   :" << setw(20) << HitCount  << "\n"
             << "Rate  :" << setw(20) << fixed << setprecision(2) << double(HitCount) / CondCount * 100.0
             << left << endl;
    }

    if (0 != ItemCount)
    {
        cerr << right
             << "---------------------------\n"
             << "Count :" << setw(20) << ItemCount << "\n"
             << "Sum   :" << setw(20) << ItemSum << "\n"
             << "Mean  :" << setw(20) << fixed << setprecision(2) << double(ItemSum) / ItemCount
             << left << endl;
    }
}
