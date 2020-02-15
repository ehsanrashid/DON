#pragma once

#include <atomic>

#include "Type.h"

// Debug class mainly collects run-time statistics and print them
class Debugger
{
    static std::atomic<u64> Hit1Count;
    static std::atomic<u64> Hit2Count;

    static std::atomic<u64> ItemCount;
    static std::atomic<i64> ItemSum;

public:
    static void reset();

    static void hitOn(bool);
    static void hitOn(bool, bool);

    static void meanOf(i64);

    static void print();

};
