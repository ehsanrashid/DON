#include "Debugger.h"

#include <iomanip>
#include <iostream>

using namespace std;

atomic<u64> Debugger::Hit1Count;
atomic<u64> Debugger::Hit2Count;

atomic<u64> Debugger::ItemCount;
atomic<i64> Debugger::ItemSum;

void Debugger::reset()
{
    Hit1Count = 0;
    Hit2Count = 0;

    ItemCount = 0;
    ItemSum = 0;
}

void Debugger::hitOn(bool hit2)
{
    ++Hit1Count;
    if (hit2)
    {
        ++Hit2Count;
    }
}

void Debugger::hitOn(bool hit1, bool hit2)
{
    if (hit1)
    {
        hitOn(hit2);
    }
}

void Debugger::meanOf(i64 item)
{
    ++ItemCount;
    ItemSum += item;
}

void Debugger::print()
{
    if (0 != Hit1Count)
    {
        cerr
            << right
            << "---------------------------\n"
            << "Hit1  :" << setw(20) << Hit1Count << "\n"
            << "Hit2  :" << setw(20) << Hit2Count << "\n"
            << "Rate  :" << setw(20) << fixed << setprecision(2) << 100.0 * Hit2Count / Hit1Count
            << left << endl;
    }
    if (0 != ItemCount)
    {
        cerr
            << right
            << "---------------------------\n"
            << "Count :" << setw(20) << ItemCount << "\n"
            << "Sum   :" << setw(20) << ItemSum << "\n"
            << "Mean  :" << setw(20) << fixed << setprecision(2) << 1.0 * ItemSum / ItemCount
            << left << endl;
    }
}
