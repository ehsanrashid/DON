/*
  DON, a UCI chess playing engine derived from Stockfish

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef MISC_H_INCLUDED
#define MISC_H_INCLUDED

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#if !defined(STRINGIFY)
    #define STRINGIFY2(x) #x
    #define STRINGIFY(x) STRINGIFY2(x)
#endif

namespace std {
template<typename T, uint32_t M, uint32_t N>
using array2d = array<array<T, M>, N>;
}

namespace DON {

std::string engine_info(bool uci = false) noexcept;
std::string compiler_info() noexcept;

// Preloads the given address in L1/L2 cache. This is a non-blocking
// function that doesn't stall the CPU waiting for data to be loaded from memory,
// which can be quite slow.
void prefetch([[maybe_unused]] void* addr) noexcept;

void start_logger(const std::string& fname) noexcept;

void* std_aligned_alloc(std::size_t alignment, std::size_t allocSize) noexcept;
void  std_aligned_free(void* mem) noexcept;
// memory aligned by page size, min alignment: 4096 bytes
void* aligned_large_pages_alloc(std::size_t allocSize) noexcept;
void  aligned_large_pages_free(void* mem) noexcept;

// Deleter for automating release of memory area
template<typename T>
struct AlignedDeleter final {
    void operator()(T* ptr) const noexcept {
        ptr->~T();
        std_aligned_free(ptr);
    }
};

template<typename T>
struct LargePageDeleter final {
    void operator()(T* ptr) const noexcept {
        ptr->~T();
        aligned_large_pages_free(ptr);
    }
};

template<typename T>
using AlignedPtr = std::unique_ptr<T, AlignedDeleter<T>>;

template<typename T>
using LargePagePtr = std::unique_ptr<T, LargePageDeleter<T>>;

#if !defined(NDEBUG)
void dbg_init() noexcept;
void dbg_hit_on(bool cond, std::uint8_t slot = 0) noexcept;
void dbg_min_of(std::int64_t value, std::uint8_t slot = 0) noexcept;
void dbg_max_of(std::int64_t value, std::uint8_t slot = 0) noexcept;
void dbg_mean_of(std::int64_t value, std::uint8_t slot = 0) noexcept;
void dbg_stdev_of(std::int64_t value, std::uint8_t slot = 0) noexcept;
void dbg_correl_of(std::int64_t value1, std::int64_t value2, std::uint8_t slot = 0) noexcept;
void dbg_print() noexcept;
#endif

enum InOut : std::uint8_t {
    IO_LOCK,
    IO_UNLOCK
};

std::ostream& operator<<(std::ostream& os, InOut io) noexcept;

#define sync_cout std::cout << IO_LOCK
#define sync_endl std::endl << IO_UNLOCK

using SystemClock  = std::chrono::system_clock;
using SteadyClock  = std::chrono::steady_clock;
using MilliSeconds = std::chrono::milliseconds;
using MicroSeconds = std::chrono::microseconds;

using TimePoint = MilliSeconds::rep;  // A value in milliseconds
static_assert(sizeof(TimePoint) == sizeof(std::int64_t), "TimePoint should be 64 bits");
inline TimePoint now() noexcept {
    return std::chrono::duration_cast<MilliSeconds>(SteadyClock::now().time_since_epoch()).count();
}

//std::ostream& operator<<(std::ostream&                            os,
//                         [[maybe_unused]] SystemClock::time_point timePoint) noexcept;

std::string format_time(std::chrono::time_point<SystemClock> timePoint);

// Get the first aligned element of an array.
// ptr must point to an array of size at least `sizeof(T) * N + alignment` bytes,
// where N is the number of elements in the array.
template<std::uintptr_t Alignment, typename T>
T* align_ptr_up(T* ptr) noexcept {
    static_assert(alignof(T) < Alignment);

    const std::uintptr_t intPtr = reinterpret_cast<std::uintptr_t>(reinterpret_cast<char*>(ptr));
    return reinterpret_cast<T*>(
      reinterpret_cast<char*>((intPtr + (Alignment - 1)) / Alignment * Alignment));
}

// True if and only if the binary is compiled on a little-endian machine
static inline const union {
    std::uint32_t i;
    char          c[4];
} Le                                    = {0x01020304};
static inline const bool IsLittleEndian = (Le.c[0] == 4);

// xorshift64star Pseudo-Random Number Generator
// This class is based on original code written and dedicated
// to the public domain by Sebastiano Vigna (2014).
// It has the following characteristics:
//
//  -  Outputs 64-bit numbers
//  -  Passes Dieharder and SmallCrush test batteries
//  -  Does not require warm-up, no zeroland to escape
//  -  Internal state is a single 64-bit integer
//  -  Period is 2^64 - 1
//  -  Speed: 1.60 ns/call (Core i7 @3.40GHz)
//
// For further analysis see
//   <http://vigna.di.unimi.it/ftp/papers/xorshift.pdf>
class PRNG final {

    std::uint64_t s;

    std::uint64_t rand64() noexcept {
        s ^= s >> 12, s ^= s << 25, s ^= s >> 27;
        return s * 0x2545F4914F6CDD1DULL;
    }

   public:
    PRNG(std::uint64_t seed) noexcept :
        s(seed) {
        assert(seed);
    }

    template<typename T>
    T rand() noexcept {
        return T(rand64());
    }

    // Special generator used to fast init magic numbers.
    // Output values only have 1/8th of their bits set on average.
    template<typename T>
    T sparse_rand() noexcept {
        return T(rand64() & rand64() & rand64());
    }
};

constexpr std::uint64_t mul_hi64(std::uint64_t a, std::uint64_t b) noexcept {
#if defined(__GNUC__) && defined(IS_64BIT)
    __extension__ using uint128 = unsigned __int128;
    return (uint128(a) * uint128(b)) >> 64;
#else
    std::uint64_t aL = std::uint32_t(a), aH = a >> 32;
    std::uint64_t bL = std::uint32_t(b), bH = b >> 32;
    std::uint64_t c1 = aH * bL + (aL * bL) >> 32;
    std::uint64_t c2 = aL * bH + std::uint32_t(c1);
    return aH * bH + (c1 >> 32) + (c2 >> 32);
#endif
}

// Under Windows it is not possible for a process to run on more than one
// logical processor group. This usually means being limited to using max 64
// cores. To overcome this, some special platform-specific API should be
// called to set group affinity for each thread. Original code from Texel by
// Peter Österlund.
namespace WinProcGroup {

void bind_thread([[maybe_unused]] std::uint16_t idx) noexcept;
}

struct CommandLine final {
    CommandLine(int ac, const char** av) noexcept :
        argc(ac),
        argv(av) {}

    static std::string get_binary_directory(const std::string& path) noexcept;
    static std::string get_working_directory() noexcept;

    int          argc;
    const char** argv;
};

inline std::string to_lower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), tolower);
    return str;
}

inline std::string to_upper(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), toupper);
    return str;
}

}  // namespace DON

#endif  // #ifndef MISC_H_INCLUDED
