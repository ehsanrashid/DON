#include "reporter.h"

#include <iomanip>
#include <iostream>
#include <sstream>

std::atomic<uint64_t> Reporter::Hit1Count{ 0 };
std::atomic<uint64_t> Reporter::Hit2Count{ 0 };

std::atomic<uint32_t> Reporter::ItemCount{ 0 };
std::atomic< int64_t> Reporter::ItemSum{ 0 };

void Reporter::reset() noexcept {

    Hit1Count = 0;
    Hit2Count = 0;

    ItemCount = 0;
    ItemSum = 0;
}

void Reporter::hitOn(bool hit2) noexcept {
    ++Hit1Count;
    if (hit2) {
        ++Hit2Count;
    }
}
void Reporter::hitOn(bool hit1, bool hit2) noexcept {
    if (hit1) {
        hitOn(hit2);
    }
}

void Reporter::meanOf(int64_t item) noexcept {
    ++ItemCount;
    ItemSum += item;
}

void Reporter::print() {

    if (Hit1Count != 0) {
        std::ostringstream oss{};
        oss << std::right
            << "---------------------------\n"
            << "Hit1  :" << std::setw(20) << Hit1Count << '\n'
            << "Hit2  :" << std::setw(20) << Hit2Count << '\n'
            << "Rate  :" << std::setw(20) << std::fixed << std::setprecision(2)
                                          << 100 * (double) Hit2Count / Hit1Count;
        std::cerr << oss.str() << '\n';
    }
    if (ItemCount != 0) {
        std::ostringstream oss{};
        oss << std::right
            << "---------------------------\n"
            << "Count :" << std::setw(20) << ItemCount << '\n'
            << "Sum   :" << std::setw(20) << ItemSum   << '\n'
            << "Mean  :" << std::setw(20) << std::fixed << std::setprecision(2)
                                          << (double) ItemSum / ItemCount;
        std::cerr << oss.str() << '\n';
    }
}
