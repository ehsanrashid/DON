#pragma once

#include <atomic>

#include "type.h"

// Debug class mainly collects run-time statistics and print them
class Debugger final {

public:

    Debugger() = delete;
    Debugger(Debugger const&) = delete;
    Debugger(Debugger&&) = delete;
    ~Debugger() = delete;

    Debugger& operator=(Debugger const&) = delete;
    Debugger& operator=(Debugger&&) = delete;

    static void reset() noexcept;

    static void hitOn(bool) noexcept;
    static void hitOn(bool, bool) noexcept;

    static void meanOf(i64) noexcept;

    static void print();

private:

    static std::atomic<u64> Hit1Count;
    static std::atomic<u64> Hit2Count;

    static std::atomic<u64> ItemCount;
    static std::atomic<i64> ItemSum;
};
