#pragma once

#include <cstdint>
#include <atomic>

// Reporter mainly collects run-time statistics and print them
namespace Reporter {

    extern void reset() noexcept;

    extern void hitOn(bool) noexcept;
    extern void hitOn(bool, bool) noexcept;

    extern void meanOf(int64_t) noexcept;

    extern void print();
}
