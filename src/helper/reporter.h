#pragma once

#include <cstdint>
#include <atomic>

// Reporter class mainly collects run-time statistics and print them
class Reporter final {

public:

    Reporter() = delete;
    Reporter(Reporter const&) = delete;
    Reporter(Reporter&&) = delete;
    ~Reporter() = delete;

    Reporter& operator=(Reporter const&) = delete;
    Reporter& operator=(Reporter&&) = delete;

    static void reset() noexcept;

    static void hitOn(bool) noexcept;
    static void hitOn(bool, bool) noexcept;

    static void meanOf(int64_t) noexcept;

    static void print();

private:

    static std::atomic<uint64_t> Hit1Count;
    static std::atomic<uint64_t> Hit2Count;

    static std::atomic<uint32_t> ItemCount;
    static std::atomic< int64_t> ItemSum;
};
