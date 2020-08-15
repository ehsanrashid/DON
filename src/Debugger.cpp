#include "Debugger.h"

#include <iomanip>
#include <iostream>
#include <sstream>

std::atomic<u64> Debugger::Hit1Count;
std::atomic<u64> Debugger::Hit2Count;

std::atomic<u64> Debugger::ItemCount;
std::atomic<i64> Debugger::ItemSum;

void Debugger::reset() {

    Hit1Count = 0;
    Hit2Count = 0;

    ItemCount = 0;
    ItemSum = 0;
}

void Debugger::hitOn(bool hit2) {
    ++Hit1Count;
    if (hit2) {
        ++Hit2Count;
    }
}
void Debugger::hitOn(bool hit1, bool hit2) {
    if (hit1) {
        hitOn(hit2);
    }
}

void Debugger::meanOf(i64 item) {
    ++ItemCount;
    ItemSum += item;
}

void Debugger::print() {
    if (Hit1Count != 0) {
        std::ostringstream oss{};
        oss << std::right
            << "---------------------------\n"
            << "Hit1  :" << std::setw(20) << Hit1Count << '\n'
            << "Hit2  :" << std::setw(20) << Hit2Count << '\n'
            << "Rate  :" << std::setw(20) << std::fixed << std::setprecision(2)
                                          << 100 * (double) Hit2Count / Hit1Count;
        std::cerr << oss.str() << std::endl;
    }
    if (ItemCount != 0) {
        std::ostringstream oss{};
        oss << std::right
            << "---------------------------\n"
            << "Count :" << std::setw(20) << ItemCount << '\n'
            << "Sum   :" << std::setw(20) << ItemSum   << '\n'
            << "Mean  :" << std::setw(20) << std::fixed << std::setprecision(2)
                                          << (double) ItemSum / ItemCount;
        std::cerr << oss.str() << std::endl;
    }
}
