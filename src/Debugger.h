#pragma once

#include <atomic>

#include "Type.h"

// Debug class mainly collects run-time statistics and print them
class Debugger {

public:
    static void reset() noexcept;

    static void hitOn(bool) noexcept;
    static void hitOn(bool, bool) noexcept;

    static void meanOf(i64) noexcept;

    static void print() noexcept;

private:
    static std::atomic<u64> Hit1Count;
    static std::atomic<u64> Hit2Count;

    static std::atomic<u64> ItemCount;
    static std::atomic<i64> ItemSum;
};
