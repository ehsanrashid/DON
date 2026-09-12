/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

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
#include <atomic>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #include "platform_win.h"  // GetCommandLineW()
#else
#endif

#if defined(__i386__) || defined(_M_IX86)
    #define X86
    #define X86_32
#elif defined(__x86_64__) || defined(_M_X64)
    #define X86
    #define X86_64
#endif

#if defined(X86) && defined(USE_PREFETCH)
    #include <xmmintrin.h>  // SSE header for _mm_prefetch() intrinsics
    #define USE_X86_PREFETCH
#endif

#define STRING_LITERAL(x) #x
#define STRINGIFY(x) STRING_LITERAL(x)

#if defined(__clang__) || defined(__GNUC__)
    #define ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
    #define ALWAYS_INLINE __forceinline
#else
    #define ALWAYS_INLINE inline
#endif

// clang-format off
#if defined(__clang__)
    #define ASSUME(cond) __builtin_assume(cond)
#elif defined(__GNUC__)
    #if __GNUC__ >= 13
        #define ASSUME(cond) __attribute__((assume(cond)))
    #else
        #define ASSUME(cond) do { if (!(cond)) __builtin_unreachable(); } while (false)
    #endif
#elif defined(_MSC_VER)
    #define ASSUME(cond) __assume(cond)
#else
    #define ASSUME(cond)
#endif
// clang-format on

#if defined(__clang__) || defined(__GNUC__)
    #define UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
    #define UNREACHABLE() __assume(false)
#else
    #define UNREACHABLE()
#endif

#if defined(__clang__) || defined(__GNUC__)
    #define RESTRICT __restrict__
#elif defined(_MSC_VER)
    #define RESTRICT __restrict
#else
    #define RESTRICT
#endif

#if !defined(NDEBUG)
    #define DEBUG_LOG(msg) std::cerr << msg << '\n'
#else
    #define DEBUG_LOG(msg) ((void) 0)
#endif

namespace DON {

using u64 = std::uint64_t;
using u32 = std::uint32_t;
using u16 = std::uint16_t;
using u8  = std::uint8_t;

using i64 = std::int64_t;
using i32 = std::int32_t;
using i16 = std::int16_t;
using i8  = std::int8_t;

using usize = std::size_t;
using isize = std::ptrdiff_t;

using uptr = std::uintptr_t;
using iptr = std::intptr_t;

#if defined(__SIZEOF_INT128__)
__extension__ using u128 = unsigned __int128;
__extension__ using i128 = signed __int128;
#endif

using NumaIndex = u16;
using CpuIndex  = u16;

using Strings     = std::vector<std::string>;
using StringViews = std::vector<std::string_view>;

// Base exception type for application-specific errors
struct Error: public std::runtime_error {
    using std::runtime_error::runtime_error;
};

namespace ConsoleColor {

// Reset
inline constexpr const char* RESET = "\033[0m";

// Regular colors
inline constexpr const char* BLACK   = "\033[30m";
inline constexpr const char* RED     = "\033[31m";
inline constexpr const char* GREEN   = "\033[32m";
inline constexpr const char* YELLOW  = "\033[33m";
inline constexpr const char* BLUE    = "\033[34m";
inline constexpr const char* MAGENTA = "\033[35m";
inline constexpr const char* CYAN    = "\033[36m";
inline constexpr const char* WHITE   = "\033[37m";

// Bright/intense colors
inline constexpr const char* BRIGHT_BLACK   = "\033[90m";  // Dark gray
inline constexpr const char* BRIGHT_RED     = "\033[91m";
inline constexpr const char* BRIGHT_GREEN   = "\033[92m";
inline constexpr const char* BRIGHT_YELLOW  = "\033[93m";
inline constexpr const char* BRIGHT_BLUE    = "\033[94m";
inline constexpr const char* BRIGHT_MAGENTA = "\033[95m";
inline constexpr const char* BRIGHT_CYAN    = "\033[96m";
inline constexpr const char* BRIGHT_WHITE   = "\033[97m";

// Text styles
inline constexpr const char* BOLD          = "\033[1m";
inline constexpr const char* DIM           = "\033[2m";
inline constexpr const char* ITALIC        = "\033[3m";
inline constexpr const char* UNDERLINE     = "\033[4m";
inline constexpr const char* BLINK         = "\033[5m";
inline constexpr const char* REVERSE       = "\033[7m";
inline constexpr const char* STRIKETHROUGH = "\033[9m";

// Background colors
inline constexpr const char* BG_BLACK   = "\033[40m";
inline constexpr const char* BG_RED     = "\033[41m";
inline constexpr const char* BG_GREEN   = "\033[42m";
inline constexpr const char* BG_YELLOW  = "\033[43m";
inline constexpr const char* BG_BLUE    = "\033[44m";
inline constexpr const char* BG_MAGENTA = "\033[45m";
inline constexpr const char* BG_CYAN    = "\033[46m";
inline constexpr const char* BG_WHITE   = "\033[47m";

}  // namespace ConsoleColor

inline constexpr usize BYTE_BITS = 8;

inline constexpr usize HEX64_SIZE = 16;
inline constexpr usize HEX32_SIZE = 8;

inline constexpr usize KB = 1024;
inline constexpr usize MB = KB * KB;

inline constexpr usize BLOCK_4  = 4;
inline constexpr usize BLOCK_8  = 2 * BLOCK_4;
inline constexpr usize BLOCK_16 = 4 * BLOCK_4;
inline constexpr usize BLOCK_32 = 8 * BLOCK_4;

inline constexpr i64 INT_LIMIT = std::numeric_limits<i32>::max();

inline constexpr double LN2   = 0.693147180559945309417232121458176568;
inline constexpr double SQRT2 = 1.41421356237309504880168872420969808;

inline constexpr std::string_view EMPTY_STRING{"<empty>"};
inline constexpr std::string_view WHITE_SPACE{" \t\n\r\f\v"};

// True if and only if the binary is compiled on a little-endian machine
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
inline constexpr bool IS_LITTLE_ENDIAN = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;
#elif defined(_WIN32)
inline constexpr bool IS_LITTLE_ENDIAN = true;
#else
// Fallback runtime check
inline const bool IS_LITTLE_ENDIAN = []() noexcept {
    constexpr u16 LE = 1;
    return *reinterpret_cast<const u8*>(&LE) == 1;
}();
#endif

constexpr u64 bit(const u8 b) noexcept { return (u64{1} << b); }

template<typename To, typename From>
constexpr bool is_strictly_assignable_v =
  std::is_assignable_v<To&, From> && (std::is_same_v<To, From> || !std::is_convertible_v<From, To>);

// Return the sign of a number (-1, 0, +1)
template<
  typename T,
  std::enable_if_t<std::is_arithmetic_v<T> || (std::is_enum_v<T> && std::is_convertible_v<T, int>),
                   int> = 0>
constexpr int sign(const T x) noexcept {
    // NaN -> 0; unsigned types never return -1
    return (T(0) < x) - (x < T(0));  // Returns 1 for positive, -1 for negative, and 0 for zero
}

// Return the square of a number, using a wider type to avoid overflow
template<typename T>
constexpr auto sqr(const T x) noexcept {
    static_assert(std::is_arithmetic_v<T>, "Argument must be arithmetic");
    using Wider = std::conditional_t<std::is_integral_v<T>, long long, T>;
    return Wider(x) * Wider(x);
}

// Return the square of a number multiplied by its sign, using a wider type to avoid overflow
template<typename T>
constexpr auto sign_sqr(const T x) noexcept {
    static_assert(std::is_arithmetic_v<T>, "Argument must be arithmetic");
    return sign(x) * sqr(x);
}

template<typename T, std::enable_if_t<std::is_integral_v<T>, bool> = true>
constexpr std::make_unsigned_t<T> constexpr_abs(const T x) noexcept {
    using U = std::make_unsigned_t<T>;
    return x < 0 ? U{} - static_cast<U>(x) : static_cast<U>(x);
}
constexpr float       constexpr_abs(const float f) noexcept { return f < 0.0f ? -f : f; }
constexpr double      constexpr_abs(const double d) noexcept { return d < 0.0 ? -d : d; }
constexpr long double constexpr_abs(const long double ld) noexcept { return ld < 0.0L ? -ld : ld; }

constexpr int constexpr_ceil(const double d) noexcept { return static_cast<int>(d + 0.4999); }
constexpr int constexpr_floor(const double d) noexcept { return static_cast<int>(d - 0.4999); }
constexpr int constexpr_round(const double d) noexcept {
    return d < 0.0 ? constexpr_floor(d) : constexpr_ceil(d);
}

// Computes ln(1 + d) for f in (-1, sqrt(2)-1] via the identity
//   ln(1+d) = 2 * atanh(s),   s = d / (d + 2.0)
//
// After the sqrt(2) range reduction below, |s| <= (sqrt(2)-1)/(sqrt(2)+1)
// = 3 - 2*sqrt(2) ≈ 0.1716, so the series needs only ~10 terms for full
// double precision (truncation error < 2e-17).
constexpr double constexpr_log1p_log(const double d) noexcept {
    double s  = d / (d + 2.0);
    double s2 = s * s;
    // clang-format off
    double p = 1.0
        + s2 * (1.0 / 3.0
        + s2 * (1.0 / 5.0
        + s2 * (1.0 / 7.0
        + s2 * (1.0 / 9.0
        + s2 * (1.0 / 11.0
        + s2 * (1.0 / 13.0
        + s2 * (1.0 / 15.0
        + s2 * (1.0 / 17.0
        + s2 *  1.0 / 19.0))))))));
    // clang-format on
    return 2.0 * s * p;
}

// constexpr natural logarithm using two-stage range reduction + atanh series.
//
// Stage 1: x = m * 2^e,  m in [1, 2)
// Stage 2: if m >= sqrt(2), halve m and increment e  =>  m in [1/sqrt(2), sqrt(2))
//
// Then  ln(x) = ln(m) + e * ln(2),  where f = m - 1 in (-0.293, 0.414).
//
// Note: the while loops are O(|exponent|) iterations, which is fine for
// compile-time table generation. For runtime use, prefer std::log.
constexpr double constexpr_log(double x) noexcept {
    // Returns an approximation of ln(x) for x > 0.
    // For x <= 0, returns -1e300 as a constexpr-safe sentinel.
    if (x <= 0.0)
        return -1e300;  // Undefined; not NaN/−inf so it stays constexpr-safe

    int exponent = 0;

    // Stage 1: reduce to [1, 2)
    while (x >= 2.0)
    {
        ++exponent;
        x *= 0.5;
    }
    while (x < 1.0)
    {
        --exponent;
        x *= 2.0;
    }

    // Stage 2: reduce to [1/sqrt(2), sqrt(2))
    // If x >= sqrt(2), folding the upper half down gives f = x - 1 closer to 0,
    // keeping |s| <= ~0.172 and making the series converge in ~10 terms.
    if (x >= SQRT2)
    {
        x *= 0.5;
        ++exponent;
    }

    // f = x - 1  in  (-0.293, 0.414)
    return constexpr_log1p_log(x - 1.0) + exponent * LN2;
}

template<typename T>
constexpr bool is_power_of_2(const T x) noexcept {
    return x != 0 && (x & (x - 1)) == 0;
}

template<typename T1, typename T2>
constexpr std::common_type_t<T1, T2> ceil_div(const T1 n, const T2 d) noexcept {
    using R = std::common_type_t<T1, T2>;
    return (R(n) + R(d) - 1) / R(d);
}

// Round n up to be a multiple of base
template<typename T>
constexpr T ceil_to_multiple(const T n, const T base) noexcept {
    return ceil_div(n, base) * base;
}

// Round up to the next power of 2
constexpr usize round_up_to_pow2(usize x) noexcept {
    if (x == 0)
        return 1;

    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
#if SIZE_MAX > 0xFFFFFFFF
    x |= x >> 32;  // for 64-bit size_t
#endif
    return x + 1;
}

// Round up to a multiple of alignment
template<typename T>
[[nodiscard]] constexpr T round_up_to_multiple(const T size, const T alignment) noexcept {
    static_assert(std::is_unsigned_v<T>, "round_up_to_multiple() requires an unsigned type");
    // Alignment must be non-zero power of 2
    assert(is_power_of_2(alignment));

    // Safely handle edge case: zero alignment when assertions are disabled
    if (alignment == 0)
        return size;

    const T mask = alignment - 1;

    if (size > std::numeric_limits<T>::max() - mask)
        return std::numeric_limits<T>::max();

    // Round up to the next multiple of alignment
    return (size + mask) & ~mask;
}

// Get the first aligned element of an array.
// ptr must point to an array of size at least 'sizeof(T) * N + alignment' bytes,
// where N is the number of elements in the array.
template<usize Alignment, typename T>
[[nodiscard]] constexpr T* align_ptr_up(T* ptr) noexcept {
    static_assert(is_power_of_2(Alignment), "Alignment must be non-zero power of 2");
    static_assert(Alignment >= alignof(T), "Alignment must be >= alignof(T)");

    const auto ptrUInt =
      round_up_to_multiple(reinterpret_cast<uptr>(ptr), static_cast<uptr>(Alignment));
    return reinterpret_cast<T*>(ptrUInt);
}

constexpr float max_load_factor(float maxLoadFactor = 0.75f) noexcept {
    return std::clamp(constexpr_abs(maxLoadFactor), 0.1f, 1.0f);
}
constexpr usize reserve_count(usize reserveCount = 1024) noexcept {
    return std::max<usize>(reserveCount, 8);
}

template<typename T1, typename T2>
constexpr T2 interpolate(T1 x, T1 x0, T1 x1, T2 y0, T2 y1) noexcept {
    assert(x0 != x1);
    return T2(y0 + (y1 - y0) * (x - x0) / (x1 - x0));
}

[[nodiscard]] constexpr bool is_idigit(const int dg) noexcept { return 0 <= dg && dg <= 9; }
[[nodiscard]] constexpr bool is_cdigit(const char ch) noexcept { return '0' <= ch && ch <= '9'; }
[[nodiscard]] constexpr bool is_space(const char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}
[[nodiscard]] constexpr bool is_lower(const char ch) noexcept { return 'a' <= ch && ch <= 'z'; }
[[nodiscard]] constexpr bool is_upper(const char ch) noexcept { return 'A' <= ch && ch <= 'Z'; }

[[nodiscard]] constexpr char lower_case(const char ch) noexcept {
    return is_upper(ch) ? char(ch + ('a' - 'A')) : ch;
}
[[nodiscard]] constexpr char upper_case(const char ch) noexcept {
    return is_lower(ch) ? char(ch - ('a' - 'A')) : ch;
}

[[nodiscard]] constexpr char digit_to_char(const int dg) noexcept {
    assert(is_idigit(dg) && "digit_to_char: non-digit integer");

    return is_idigit(dg) ? dg + '0' : '\0';
}
[[nodiscard]] constexpr int char_to_digit(const char ch) noexcept {
    assert(is_cdigit(ch) && "char_to_digit: non-digit character");

    return is_cdigit(ch) ? ch - '0' : 0;
}

constexpr unsigned to_month(const std::string_view mon) noexcept {
    assert(mon.size() == 3);
    return lower_case(mon[0]) == 'j' && lower_case(mon[1]) == 'a' ? 1
         : lower_case(mon[0]) == 'f'                              ? 2
         : lower_case(mon[0]) == 'm' && lower_case(mon[2]) == 'r' ? 3
         : lower_case(mon[0]) == 'a' && lower_case(mon[1]) == 'p' ? 4
         : lower_case(mon[0]) == 'm' && lower_case(mon[2]) == 'y' ? 5
         : lower_case(mon[0]) == 'j' && lower_case(mon[2]) == 'n' ? 6
         : lower_case(mon[0]) == 'j' && lower_case(mon[2]) == 'l' ? 7
         : lower_case(mon[0]) == 'a' && lower_case(mon[1]) == 'u' ? 8
         : lower_case(mon[0]) == 's'                              ? 9
         : lower_case(mon[0]) == 'o'                              ? 10
         : lower_case(mon[0]) == 'n'                              ? 11
         : lower_case(mon[0]) == 'd'                              ? 12
                                                                  : 0;
}

enum class ConsoleMode : u8 {
    Default,  // Do nothing special
    UTF7,     // Explicitly avoid UTF-8 changes
    UTF8,     // Try to enable UTF-8 if possible
    EnableVirtualTerminal,
    FullyFeatured,
};

void set_console_input(ConsoleMode consoleMode = ConsoleMode::Default) noexcept;

void set_console_output(ConsoleMode consoleMode = ConsoleMode::Default) noexcept;

std::string format_date(std::string_view date) noexcept;

std::string format_time(std::string_view time) noexcept;

std::string build_date() noexcept;

std::string build_time() noexcept;

std::string build_timestamp() noexcept;

std::string engine_info(bool uci = false) noexcept;

std::string engine_logo() noexcept;

std::string version_info() noexcept;

std::string compiler_info() noexcept;

using SteadyClock = std::chrono::steady_clock;
using SystemClock = std::chrono::system_clock;
using Us          = std::chrono::microseconds;
using Ms          = std::chrono::milliseconds;

using TimePoint = Ms::rep;  // A value in milliseconds
static_assert(sizeof(TimePoint) == sizeof(i64), "TimePoint size must be 8 bytes");

inline TimePoint now() noexcept {
    return std::chrono::duration_cast<Ms>(SteadyClock::now().time_since_epoch()).count();
}

std::string format_time(const SystemClock::time_point& timePoint) noexcept;

constexpr u64 mul_hi64(const u64 u1, const u64 u2) noexcept {
#if defined(__SIZEOF_INT128__)
    return (static_cast<u128>(u1) * static_cast<u128>(u2)) >> 64;
#else
    u64 u1L = static_cast<u32>(u1), u1H = u1 >> 32;
    u64 u2L = static_cast<u32>(u2), u2H = u2 >> 32;
    u64 mid = u1H * u2L + ((u1L * u2L) >> 32);
    return u1H * u2H + ((u1L * u2H + static_cast<u32>(mid)) >> 32) + (mid >> 32);
#endif
}

static_assert(mul_hi64(u64{0xDEADBEEFDEADBEEF}, u64{0xCAFEBABECAFEBABE}) == u64{0xB092AB7CE9F4B259},
              "mul_hi64(): Failed");

// PrefetchAccess for explicit call-site control
enum class PrefetchAccess : u8 {
    READ,
    WRITE
};

// PrefetchLoc controls locality / cache level, not whether a prefetch is issued.
// In particular, PrefetchLoc::NONE maps to a non-temporal / lowest-locality prefetch
// (Intel: _MM_HINT_NTA, GCC/Clang: locality = 0) and therefore still performs a prefetch.
enum class PrefetchLoc : u8 {
    NONE,      // Non-temporal / no cache locality (still issues a prefetch)
    LOW,       // Low locality (e.g. T2 / L2)
    MODERATE,  // Moderate locality (e.g. T1 / L1)
    HIGH       // High locality (e.g. T0 / closest cache)
};

#if defined(USE_PREFETCH)
// Preloads the given address into cache.
// Non-blocking operation that doesn't stall the CPU waiting for data to be loaded from memory.
// NOTE:
// On x86, _mm_prefetch() does NOT truly distinguish READ vs WRITE.
// PrefetchAccess::WRITE is a best-effort hint only and may behave identically to READ.
// On GCC/Clang, __builtin_prefetch supports Access as a separate hint.
template<PrefetchAccess Access = PrefetchAccess::READ, PrefetchLoc Loc = PrefetchLoc::HIGH>
inline void prefetch(const void* addr) noexcept {
    #if defined(USE_X86_PREFETCH)
    constexpr auto Hint = []() constexpr noexcept {
        if constexpr (Access == PrefetchAccess::WRITE)
            return
        #if defined(_MM_HINT_ET0)
              _MM_HINT_ET0
        #else
              _MM_HINT_T0
        #endif
              ;
        if constexpr (Loc == PrefetchLoc::NONE)
            return _MM_HINT_NTA;
        if constexpr (Loc == PrefetchLoc::LOW)
            return _MM_HINT_T2;
        if constexpr (Loc == PrefetchLoc::MODERATE)
            return _MM_HINT_T1;
        return _MM_HINT_T0;  // PrefetchLoc::HIGH
    }();
    _mm_prefetch(reinterpret_cast<const char*>(addr), Hint);
    #elif defined(__GNUC__) || defined(__clang__)
    constexpr int RW       = Access == PrefetchAccess::READ ? 0  //
                                                            : 1;
    constexpr int Locality = Loc == PrefetchLoc::NONE     ? 0
                           : Loc == PrefetchLoc::LOW      ? 1
                           : Loc == PrefetchLoc::MODERATE ? 2
                                                          : 3;  // PrefetchLoc::HIGH
    __builtin_prefetch(addr, RW, Locality);
    #else
    // No-op on unsupported platforms
    (void) addr;
    #endif
}
#else
template<PrefetchAccess Access = PrefetchAccess::READ, PrefetchLoc Loc = PrefetchLoc::HIGH>
inline void prefetch(const void*) noexcept {}
#endif

struct CallOnce final {
   public:
    CallOnce() noexcept = default;

    // Initialize using the provided function
    // The function will be called exactly once, even if multiple threads call this
    template<typename Func>
    void operator()(Func&& callFn) noexcept(noexcept(callFn())) {
        std::call_once(onceFlag, [this, callFunc = std::forward<Func>(callFn)]() mutable {
            std::move(callFunc)();  // Move into the call
            initialize.store(true, std::memory_order_release);
        });
    }

    // Check if initialization has been completed
    [[nodiscard]] bool initialized() const noexcept {
        return initialize.load(std::memory_order_acquire);
    }

   private:
    CallOnce(const CallOnce&) noexcept            = delete;
    CallOnce& operator=(const CallOnce&) noexcept = delete;
    CallOnce(CallOnce&&) noexcept                 = delete;
    CallOnce& operator=(CallOnce&&) noexcept      = delete;

    std::once_flag    onceFlag;
    std::atomic<bool> initialize{false};
};

namespace OstreamMutexRegistry {

std::mutex& get(std::ostream* osPtr) noexcept;

}  // namespace OstreamMutexRegistry

// SyncOstream
//
// Provides RAII-based, thread-safe output synchronization for a std::ostream.
//
// Each SyncOstream acquires the mutex associated with the given ostream through
// OstreamMutexRegistry and holds the lock for its lifetime.
//
// Key Features:
//  - Thread-safe: synchronizes access to the associated ostream.
//  - RAII-based: acquires the mutex on construction and releases it on destruction.
//  - Move-constructible: allows SyncOstream objects to be returned by value.
//  - Non-copyable and non-move-assignable: prevents accidental lock ownership changes.
//  - Supports standard ostream operators and manipulators.
//  - Asserts on use of a moved-from SyncOstream in debug builds.
//
// Usage:
//   SyncOstream(std::cout) << "Thread-safe message " << value << std::endl;
//
// Notes:
//  - The lock is held for the lifetime of the SyncOstream object.
//  - Keep SyncOstream objects short-lived to minimize lock contention.
//  - OstreamMutexRegistry ensures that the same ostream uses the same mutex.
class [[nodiscard]] SyncOstream final {
   public:
    explicit SyncOstream(std::ostream& os) noexcept;

    SyncOstream(const SyncOstream&) noexcept            = delete;
    SyncOstream& operator=(const SyncOstream&) noexcept = delete;
    // Move-constructible so SyncOstream objects can be returned by value
    SyncOstream(SyncOstream&& syncOs) noexcept;
    // Move-assignment is deleted to prevent changing lock ownership
    SyncOstream& operator=(SyncOstream&&) noexcept = delete;

    using IosManip = std::ios& (*) (std::ios&);

    SyncOstream&  operator<<(IosManip manip) &;
    SyncOstream&& operator<<(IosManip manip) &&;

    using OstreamManip = std::ostream& (*) (std::ostream&);

    SyncOstream&  operator<<(OstreamManip manip) &;
    SyncOstream&& operator<<(OstreamManip manip) &&;

    template<typename T>
    SyncOstream& operator<<(T&& x) & {
        assert(osPtr != nullptr && "Use of moved-from SyncOstream");

        *osPtr << std::forward<T>(x);
        return *this;
    }
    template<typename T>
    SyncOstream&& operator<<(T&& x) && {
        assert(osPtr != nullptr && "Use of moved-from SyncOstream");

        *osPtr << std::forward<T>(x);
        return std::move(*this);
    }

   private:
    std::ostream*                osPtr;
    std::unique_lock<std::mutex> lock;
};

[[nodiscard]] SyncOstream sync_os(std::ostream& os = std::cout) noexcept;

struct IndexRange final {
   public:
    usize beg;
    usize end;
};

// Split [0, size) into 'count' contiguous, nearly equal ranges.
// 'id' identifies the range to return and must be in [0, count).
constexpr IndexRange split_range(const usize id, const usize count, const usize size) noexcept {
    assert(count != 0 && id < count);

    const usize base  = size / count;
    const usize extra = size % count;

    // Distribute 'size' as evenly as possible: the first 'extra' ranges
    // get one additional element, and 'beg' accounts for preceding extras.
    const usize beg = id * base + std::min(id, extra);
    const usize end = beg + base + usize(id < extra);

    assert(beg <= end && end <= size);
    return {beg, end};
}

// --- TableView with pointer and size ---
template<typename T>
class TableView final {
   public:
    constexpr TableView(T* data, usize size) noexcept :
        data_(data),
        size_(size) {}

    constexpr T*       data() noexcept { return data_; }
    constexpr const T* data() const noexcept { return data_; }

    [[nodiscard]] constexpr usize size() const noexcept { return size_; }

    constexpr T* begin() noexcept { return data(); }
    constexpr T* end() noexcept { return begin() + size(); }
    constexpr T* begin() const noexcept { return data(); }
    constexpr T* end() const noexcept { return begin() + size(); }

    constexpr T& operator[](const usize idx) noexcept {
        assert(idx < size());
        return data_[idx];
    }
    constexpr T& operator[](const usize idx) const noexcept {
        assert(idx < size());
        return data_[idx];
    }

   private:
    T*    data_ = nullptr;
    usize size_ = 0;
};

template<typename T, usize Size, usize... Sizes>
class MultiArray;

namespace Internal {

template<typename T, usize Size, usize... Sizes>
struct ArrayDef final {
    static_assert(Size >= 0, "dimension must be >= 0");
    using type = std::array<typename ArrayDef<T, Sizes...>::type, Size>;
};

template<typename T, usize Size>
struct ArrayDef<T, Size> final {
    static_assert(Size >= 0, "dimension must be >= 0");
    using type = std::array<T, Size>;
};

// Recursive template to define multi-dimensional array
template<typename T, usize Size, usize... Sizes>
struct MultiArrayDef final {
    static_assert(Size >= 0, "dimension must be >= 0");
    using Type = MultiArray<T, Sizes...>;
};
// Base case: single-dimensional array
template<typename T, usize Size>
struct MultiArrayDef<T, Size> final {
    static_assert(Size >= 0, "dimension must be >= 0");
    using Type = T;
};

}  // namespace Internal

template<typename T, usize Size, usize... Sizes>
using Array = typename Internal::ArrayDef<T, Size, Sizes...>::type;

// MultiArray is a generic N-dimensional array.
// The template parameter T is the base type of the MultiArray
// The template parameters (Size and Sizes) is the dimensions of the MultiArray.
template<typename T, usize Size, usize... Sizes>
class MultiArray final {
    using ElementType = typename Internal::MultiArrayDef<T, Size, Sizes...>::Type;
    using ArrayType   = Array<ElementType, Size>;

   public:
    using value_type             = typename ArrayType::value_type;
    using size_type              = typename ArrayType::size_type;
    using difference_type        = typename ArrayType::difference_type;
    using reference              = typename ArrayType::reference;
    using const_reference        = typename ArrayType::const_reference;
    using pointer                = typename ArrayType::pointer;
    using const_pointer          = typename ArrayType::const_pointer;
    using iterator               = typename ArrayType::iterator;
    using const_iterator         = typename ArrayType::const_iterator;
    using reverse_iterator       = typename ArrayType::reverse_iterator;
    using const_reverse_iterator = typename ArrayType::const_reverse_iterator;

    constexpr auto begin() const noexcept { return data_.begin(); }
    constexpr auto end() const noexcept { return data_.end(); }
    constexpr auto begin() noexcept { return data_.begin(); }
    constexpr auto end() noexcept { return data_.end(); }

    constexpr auto cbegin() const noexcept { return data_.cbegin(); }
    constexpr auto cend() const noexcept { return data_.cend(); }

    constexpr auto rbegin() const noexcept { return data_.rbegin(); }
    constexpr auto rend() const noexcept { return data_.rend(); }
    constexpr auto rbegin() noexcept { return data_.rbegin(); }
    constexpr auto rend() noexcept { return data_.rend(); }

    constexpr auto crbegin() const noexcept { return data_.crbegin(); }
    constexpr auto crend() const noexcept { return data_.crend(); }

    constexpr auto&       front() noexcept { return data_.front(); }
    constexpr const auto& front() const noexcept { return data_.front(); }
    constexpr auto&       back() noexcept { return data_.back(); }
    constexpr const auto& back() const noexcept { return data_.back(); }

    auto*       data() noexcept { return data_.data(); }
    const auto* data() const noexcept { return data_.data(); }

    constexpr auto max_size() const noexcept { return data_.max_size(); }

    constexpr auto size() const noexcept { return data_.size(); }
    constexpr auto empty() const noexcept { return data_.empty(); }

    constexpr const auto& at(const size_type idx) const { return data_.at(idx); }
    constexpr auto&       at(const size_type idx) { return data_.at(idx); }

    constexpr const auto& operator[](const size_type idx) const noexcept {
        assert(idx < size());

        return data_[idx];
    }
    constexpr auto& operator[](const size_type idx) noexcept {
        assert(idx < size());

        return data_[idx];
    }

    // Recursively fill all dimensions by calling the sub fill method
    template<typename U>
    void fill(const U& v) noexcept {
        static_assert(is_strictly_assignable_v<T, U>, "Cannot assign fill value to element type");

        for (auto& element : data_)
        {
            if constexpr (sizeof...(Sizes) == 0)
                element = v;
            else
                element.fill(v);
        }
    }

    template<typename U>
    void fill_n(const usize beg, const usize count, const U& v) noexcept {
        static_assert(is_strictly_assignable_v<T, U>, "Cannot assign fill value to element type");

        usize end = std::min(beg + count, size());
        assert(beg <= end && end <= size());

        for (usize idx = beg; idx < end; ++idx)
        {
            if constexpr (sizeof...(Sizes) == 0)
                data_[idx] = v;
            else
                data_[idx].fill(v);
        }
    }

    //void print() const noexcept {
    //    std::cout << Size << ':' << sizeof...(Sizes) << '\n';
    //
    //    for (auto& element : data_)
    //    {
    //        if constexpr (sizeof...(Sizes) == 0)
    //            std::cout << element << ' ';
    //        else
    //            element.print();
    //    }
    //
    //    std::cout << std::endl;
    //}

    constexpr void swap(MultiArray<T, Size, Sizes...>& multiArr) noexcept {
        data_.swap(multiArr.data_);
    }

    template<bool NoExtraDimension = sizeof...(Sizes) == 0,
             typename              = std::enable_if_t<NoExtraDimension, bool>>
    constexpr operator Array<T, Size>&() noexcept {
        return data_;
    }
    template<bool NoExtraDimension = sizeof...(Sizes) == 0,
             typename              = std::enable_if_t<NoExtraDimension, bool>>
    constexpr operator const Array<T, Size>&() const noexcept {
        return data_;
    }

    constexpr MultiArray& operator=(const Array<T, Size, Sizes...>& stdArr) noexcept {
        for (usize i = 0; i < Size; ++i)
            data_[i] = stdArr[i];
        return *this;
    }

   private:
    ArrayType data_;
};

template<typename T, usize Capacity, typename SizeType = usize>
class FixedVector final {
    static_assert(Capacity > 0, "Capacity must be > 0");

   public:
    [[nodiscard]] static constexpr SizeType capacity() noexcept { return Capacity; }

    [[nodiscard]] constexpr SizeType size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool     empty() const noexcept { return size() == 0; }
    [[nodiscard]] constexpr bool     full() const noexcept { return size() == capacity(); }

    T*       data() noexcept { return data_.data(); }
    const T* data() const noexcept { return data_.data(); }

    T*       begin() noexcept { return data(); }
    const T* begin() const noexcept { return data(); }

    T*       end() noexcept { return begin() + size(); }
    const T* end() const noexcept { return begin() + size(); }

    const T* cbegin() const noexcept { return data(); }
    const T* cend() const noexcept { return cbegin() + size(); }

    void push_back(const T& value) noexcept {
        assert(size() < capacity());

        data_[size_++] = value;
    }
    void push_back(T&& value) noexcept {
        assert(size() < capacity());

        data_[size_++] = std::move(value);
    }
    template<typename... Args>
    void emplace_back(Args&&... args) noexcept {
        assert(size() < capacity());

        data_[size_++] = T(std::forward<Args>(args)...);
    }

    // Append value if value < max
    void push_back_if(const T& value, const bool cond) noexcept {
        assert(size() < capacity());

        data_[size_] = value;
        size_ += int(cond);
    }

    // Append value if value < max
    void push_back_if_lt(const T& value, const T& maxValue) noexcept {
        assert(size() < capacity());

        data_[size_] = value;
        size_ += int(value < maxValue);
    }

    void pop_back() noexcept {
        assert(size() != 0);

        --size_;
    }

    T& back() noexcept {
        assert(size() > 0);

        return data_[size_ - 1];
    }
    const T& back() const noexcept {
        assert(size() > 0);

        return data_[size_ - 1];
    }

    T& operator[](const SizeType idx) noexcept {
        assert(idx < size());

        return data_[idx];
    }
    const T& operator[](const SizeType idx) const noexcept {
        assert(idx < size());

        return data_[idx];
    }

    void resize(const SizeType newSize) noexcept {
        // Note: doesn't construct/destroy elements
        size_ = std::min(newSize, capacity());
    }

    T* make_space(const SizeType space) noexcept {
        SizeType oldSize = size();

        resize(oldSize + space);

        return data() + oldSize;
    }

    void clear() noexcept { size_ = 0; }

   private:
    Array<T, Capacity> data_;
    SizeType           size_ = 0;
};

struct FixedText final {
   public:
    static FixedText from(const std::string_view sv) noexcept;

    FixedText& write(char ch) noexcept;

    FixedText& write(std::string_view sv) noexcept;

    FixedText& write(int v) noexcept;

    [[nodiscard]] constexpr usize capacity() const noexcept { return data_.size(); }

    char*       begin() noexcept { return data(); }
    const char* begin() const noexcept { return data(); }

    char*       end() noexcept { return data() + size(); }
    const char* end() const noexcept { return data() + size(); }

    char*       data() noexcept { return data_.data(); }
    const char* data() const noexcept { return data_.data(); }

    [[nodiscard]] const char* c_str() const noexcept { return data_.data(); }

    [[nodiscard]] usize size() const noexcept { return size_; }
    [[nodiscard]] bool  empty() const noexcept { return size() == 0; }

    [[nodiscard]] std::string_view view() const noexcept { return {data(), size()}; }

    // implicit conversion if you want
    operator std::string_view() const noexcept { return view(); }

   private:
    Array<char, 31> data_{};
    u8              size_ = 0;
};

static_assert(sizeof(FixedText) == 32, "FixedText size must be 32 bytes");

std::ostream& operator<<(std::ostream& os, const FixedText& fixedText) noexcept;

// Wrapper around std::atomic<T> that uses relaxed atomic or plain accesses, depending on the configuration.
// Intended for platforms such as WebAssembly, where the overhead of atomic instructions can be significant
// and only non-tearing accesses are required for the updates, while ensuring we use relaxed accesses otherwise.
template<typename T>
class RelaxedAtomic final {
   public:
    RelaxedAtomic() = default;

    RelaxedAtomic(T v) noexcept :
        value(v) {}

    RelaxedAtomic(const RelaxedAtomic& relaxedAtomic) noexcept :
        value(static_cast<T>(relaxedAtomic)) {}
    RelaxedAtomic& operator=(const RelaxedAtomic& relaxedAtomic) noexcept {
        if (this == &relaxedAtomic)
            return *this;

        store(static_cast<T>(relaxedAtomic));
        return *this;
    }

    T operator=(T v) noexcept {
        store(v);
        return v;
    }

    operator T() const noexcept { return load(); }

    RelaxedAtomic& operator+=(T v) noexcept {
        add(v);
        return *this;
    }
    RelaxedAtomic& operator-=(T v) noexcept {
        sub(v);
        return *this;
    }

    RelaxedAtomic& operator++() noexcept {
        add(1);
        return *this;
    }
    RelaxedAtomic& operator--() noexcept {
        sub(1);
        return *this;
    }

    T operator++(int) noexcept { return add(1); }
    T operator--(int) noexcept { return sub(1); }

    T load() const noexcept {
        if constexpr (UseAtomic)
            return value.load(std::memory_order_relaxed);
        else
            return value;
    }
    void store(T v) noexcept {
        if constexpr (UseAtomic)
            value.store(v, std::memory_order_relaxed);
        else
            value = v;
    }

    T exchange(T v) noexcept {
        if constexpr (UseAtomic)
            return value.exchange(v, std::memory_order_relaxed);
        else
        {
            T oldV = value;
            value  = v;
            return oldV;
        }
    }

    bool compare_exchange_weak(T& expected, T desired) noexcept {
        if constexpr (UseAtomic)
            return value.compare_exchange_weak(expected, desired, std::memory_order_relaxed,
                                               std::memory_order_relaxed);
        else
        {
            if (value == expected)
            {
                value = desired;
                return true;
            }

            expected = value;
            return false;
        }
    }

   private:
    static constexpr bool UseAtomic =
#if defined(USE_SLOPPY_ATOMICS)
      !std::atomic<T>::is_always_lock_free || sizeof(T) > sizeof(usize)
#else
      true
#endif
      ;

    T add(T v) noexcept {
        const T oldV = load();
        const T newV = oldV + v;
        store(newV);
        return oldV;
    }
    T sub(T v) noexcept {
        const T oldV = load();
        const T newV = oldV - v;
        store(newV);
        return oldV;
    }

    std::conditional_t<UseAtomic, std::atomic<T>, T> value;
};

// Tracks allocation sizes and performs allocation and freeing.
template<typename AllocFunc, typename FreeFunc>
class AllocationSizes final {
   public:
    explicit AllocationSizes(AllocFunc allocFn, FreeFunc freeFn) noexcept :
        allocFunc(std::move(allocFn)),
        freeFunc(std::move(freeFn)) {}

    [[nodiscard]] void* alloc(const usize allocSize) noexcept {
        void* mem = allocFunc(allocSize);

        if (mem != nullptr)
        {
            std::lock_guard writeLock(mutex);

            sizesMap[mem] = allocSize;
        }

        return mem;
    }

    [[nodiscard]] bool free(void* const mem) noexcept {
        std::lock_guard writeLock(mutex);

        if (auto itr = sizesMap.find(mem); itr != sizesMap.end())
        {
            if (!freeFunc(mem, itr->second))
                return false;

            sizesMap.erase(itr);
            return true;
        }

        return false;
    }

    [[nodiscard]] usize size() const noexcept {
        std::shared_lock readLock(mutex);

        return sizesMap.size();
    }

    [[nodiscard]] bool empty() const noexcept {
        std::shared_lock readLock(mutex);

        return sizesMap.empty();
    }

    [[nodiscard]] std::optional<usize> find(void* const mem) const noexcept {
        std::shared_lock readLock(mutex);

        if (auto itr = sizesMap.find(mem); itr != sizesMap.end())
            return itr->second;

        return std::nullopt;
    }

   private:
    const AllocFunc                  allocFunc;
    const FreeFunc                   freeFunc;
    mutable std::shared_mutex        mutex;
    std::unordered_map<void*, usize> sizesMap;
};

// ConcurrentCache: thread-safe key-value cache with pre-reserved storage
template<typename Key, typename Value>
class ConcurrentCache final {
   public:
    explicit ConcurrentCache(usize reserveCount = 1024, float maxLoadFactor = 0.75f) noexcept {
        cacheMap.max_load_factor(max_load_factor(maxLoadFactor));
        cacheMap.reserve(reserve_count(reserveCount));
    }

    template<typename... Args>
    Value& access_or_build(const Key& key, Args&&... args) noexcept {
        // Fast path: shared read lock to check and access
        {
            std::shared_lock readLock(mutex);

            if (auto itr = cacheMap.find(key); itr != cacheMap.end())
                return get_value(itr->second);
        }

        // Slow path: exclusive write lock to insert and construct
        std::lock_guard writeLock(mutex);

        // Double-check after acquiring exclusive lock
        auto [itr, inserted] = cacheMap.try_emplace(key);

        if (inserted)
            // Inserted: construct the value
            set_value(itr->second, std::forward<Args>(args)...);

        return get_value(itr->second);
    }

    template<typename Transformer, typename... Args>
    auto
    transform_access_or_build(const Key& key, Transformer&& transformer, Args&&... args) noexcept {
        return std::forward<Transformer>(transformer)(
          access_or_build(key, std::forward<Args>(args)...));
    }

   private:
    static constexpr usize ThresholdSize = 128;

    // Define StorageValue type alias
    using StorageValue =
      std::conditional_t<sizeof(Value) <= ThresholdSize, Value, std::unique_ptr<Value>>;

    // Helper functions AFTER StorageValue is defined
    template<typename... Args>
    void set_value(StorageValue& entry, Args&&... args) {
        if constexpr (sizeof(Value) <= ThresholdSize)
            entry = Value(std::forward<Args>(args)...);
        else
            entry = std::make_unique<Value>(std::forward<Args>(args)...);
    }

    static Value& get_value(StorageValue& entry) noexcept {
        if constexpr (sizeof(Value) <= ThresholdSize)
            return entry;
        else
            return *entry;
    }

    std::shared_mutex                     mutex;
    std::unordered_map<Key, StorageValue> cacheMap;
};

// Hash function based on public domain MurmurHash64A by Austin Appleby.
// Fast, non-cryptographic 64-bit hash suitable for general-purpose hashing.
inline u64 hash_bytes(const char* RESTRICT data, usize size, u64 seed = 0) noexcept {
    constexpr u64 MurmurM = u64{0xC6A4A7935BD1E995};
    constexpr u8  MurmurR = 47;

    // Mix 64-bit word using the MurmurHash64A mixing step
    constexpr auto mix = [](u64 k) noexcept {
        k *= MurmurM;
        k ^= k >> MurmurR;
        k *= MurmurM;
        return k;
    };

    // Initialize hash with seed and length (MurmurHash64A convention)
    u64 h = seed ^ (size * MurmurM);

    const auto* const RESTRICT beg = reinterpret_cast<const u8*>(data);
    const auto* const RESTRICT end = beg + size;
    const auto* RESTRICT       p   = beg;

    // Process 32-byte blocks (4 × 64-bit lanes) for better throughput.
    // The end pointer is rounded down to the nearest multiple of BLOCK_32.
    const auto* const RESTRICT block32End = beg + (size & ~(BLOCK_32 - 1));
    for (; p < block32End; p += BLOCK_32)
    {
        u64 k0 = 0, k1 = 0, k2 = 0, k3 = 0;
        // Unaligned loads are safe via memcpy and typically optimized by the compiler
        std::memcpy(&k0, p + 0 * BLOCK_8, BLOCK_8);
        std::memcpy(&k1, p + 1 * BLOCK_8, BLOCK_8);
        std::memcpy(&k2, p + 2 * BLOCK_8, BLOCK_8);
        std::memcpy(&k3, p + 3 * BLOCK_8, BLOCK_8);

        k0 = mix(k0);
        k1 = mix(k1);
        k2 = mix(k2);
        k3 = mix(k3);
        // Merge each mixed lane into the running hash
        h ^= k0;
        h *= MurmurM;
        h ^= k1;
        h *= MurmurM;
        h ^= k2;
        h *= MurmurM;
        h ^= k3;
        h *= MurmurM;
    }
    // Process 16-byte blocks (2 × 64-bit words) for better throughput.
    // The end pointer is rounded down to the nearest multiple of BLOCK_16.
    const auto* const RESTRICT block16End = p + ((end - p) & ~(BLOCK_16 - 1));
    for (; p < block16End; p += BLOCK_16)
    {
        u64 k0 = 0, k1 = 0;
        // Unaligned loads are safe via memcpy and typically optimized by the compiler
        std::memcpy(&k0, p + 0 * BLOCK_8, BLOCK_8);
        std::memcpy(&k1, p + 1 * BLOCK_8, BLOCK_8);

        k0 = mix(k0);
        k1 = mix(k1);
        // Merge each word into the running hash
        h ^= k0;
        h *= MurmurM;
        h ^= k1;
        h *= MurmurM;
    }
    // Process remaining full 8-byte blocks.
    // The end pointer is rounded down to the nearest multiple of BLOCK_8.
    const auto* const RESTRICT block8End = p + ((end - p) & ~(BLOCK_8 - 1));
    for (; p < block8End; p += BLOCK_8)
    {
        u64 k = 0;
        // Safe unaligned load
        std::memcpy(&k, p, BLOCK_8);

        k = mix(k);
        // Merge block into the running hash
        h ^= k;
        h *= MurmurM;
    }
    // Handle remaining tail bytes (< 8) at the end
    if (p < end)
    {
        u64 k = 0;

        u8 shift = 0;
        // Read remaining bytes in little-endian order
        for (; p < end; ++p)
        {
            k |= static_cast<u64>(*p) << shift;

            shift += BYTE_BITS;
        }
        // Merge into the running hash
        h ^= k;
        h *= MurmurM;
    }

    // Final avalanche mix to ensure strong bit diffusion
    h ^= h >> MurmurR;
    h *= MurmurM;
    h ^= h >> MurmurR;

    return h;
}

inline u64 hash_string(std::string_view sv) noexcept { return hash_bytes(sv.data(), sv.size()); }

template<typename T>
u64 hash_raw_data(const T& value) noexcept {
    // Must have no padding bytes because reinterpreting as char*
    static_assert(std::has_unique_object_representations<T>());

    return hash_bytes(reinterpret_cast<const char*>(&value), sizeof(value));
}

template<typename T>
void combine_hash(usize& seed, const T& v) noexcept {
    usize x;
    // For primitive types we avoid using the default hasher, which may be
    // nondeterministic across program invocations
    if constexpr (std::is_integral<T>())
        x = v;
    else
        x = std::hash<T>{}(v);

    seed ^= x + 0x9E3779B9u + (seed << 6) + (seed >> 2);
}

constexpr u32 combine_hashes(std::initializer_list<u32> hashes) noexcept {
    u32 h = 0;
    for (const auto hash : hashes)
    {
        h = (h << 1) | (h >> 31);
        h ^= hash;
    }
    return h;
}

// Custom streambuf that wraps string_view
class StringViewBuf final: public std::streambuf {
   public:
    explicit StringViewBuf(std::string_view sv) noexcept;
};

// Custom streambuf that wraps memory stream
class MemoryBuf final: public std::streambuf {
   public:
    MemoryBuf(char* p, usize size) noexcept;
};

// TieBuf (Fancy logging facility).
// Replaces std::cin.rdbuf() and std::cout.rdbuf() with two TieBuf objects
// that preserve normal I/O while optionally mirroring input and output to a
// secondary stream buffer.
// Logging of std::cin and std::cout can be toggled at runtime without changing
// their usual I/O usage.
// Idea from http://groups.google.com/group/comp.lang.c++/msg/1d941c0f26ea0d81
// MSVC requires separate stream buffers for std::cin and std::cout.
//
// Forwards I/O to the primary buffer and mirrors it to the secondary buffer
// with line prefixes.
class TieBuf final: public std::streambuf {
   public:
    using traits_type = std::streambuf::traits_type;
    using int_type    = traits_type::int_type;
    using char_type   = traits_type::char_type;

    TieBuf() noexcept = delete;
    TieBuf(std::streambuf* pB, std::streambuf* mB) noexcept;

    int sync() override;

    int_type underflow() override;

    int_type overflow(const int_type ch) override;

    int_type uflow() override;

    std::streamsize xsputn(const char_type* s, std::streamsize count) override;

    [[nodiscard]] std::streambuf* pbuf() const noexcept;
    [[nodiscard]] std::streambuf* mbuf() const noexcept;

   private:
    int_type
    mirror_put_with_prefix(int_type ch, std::string_view prefix, char_type& preCh) noexcept;

    std::streambuf *pBuf, *mBuf;

    char_type oPreCh = '\n';
    char_type iPreCh = '\n';
};

// Logger
//
// Manages runtime logging by redirecting std::cin and std::cout through
// TieBuf objects to mirror I/O to a log file.
class Logger final {
   public:
    static bool start(const std::filesystem::path& logFile) noexcept;

    static void stop() noexcept;

   private:
    Logger() noexcept = delete;
    Logger(std::istream& isRef, std::ostream& osRef) noexcept;

    ~Logger() noexcept;

    static Logger& instance() noexcept;

    bool open(const std::filesystem::path& logFile) noexcept;

    void close() noexcept;

    [[nodiscard]] bool is_open() const noexcept;

    void write_timestamp(std::string_view suffix) noexcept;

    std::mutex      mutex;
    std::ofstream   ofs;
    std::istream&   is;
    std::ostream&   os;
    std::streambuf *isBuf = nullptr, *osBuf = nullptr;
    TieBuf          itieBuf, otieBuf;
    std::string     filename;
};

#if !defined(NDEBUG)
namespace Debug {

void clear() noexcept;
void hit_on(bool cond, usize slot = 0) noexcept;
void min_of(i64 value, usize slot = 0) noexcept;
void max_of(i64 value, usize slot = 0) noexcept;
void extreme_of(i64 value, usize slot = 0) noexcept;
void mean_of(i64 value, usize slot = 0) noexcept;
void stdev_of(i64 value, usize slot = 0) noexcept;
void correl_of(i64 value1, i64 value2, usize slot = 0) noexcept;

void print() noexcept;
}  // namespace Debug
#endif

struct CommandLine final {
   public:
    CommandLine(int argc, const char* argv[]) noexcept;
    CommandLine(const CommandLine&)            = delete;
    CommandLine& operator=(const CommandLine&) = delete;
    CommandLine(CommandLine&&)                 = default;
    CommandLine& operator=(CommandLine&&)      = default;

    static std::filesystem::path binary_directory(std::filesystem::path path) noexcept;
    static std::filesystem::path working_directory() noexcept;

    [[nodiscard]] const StringViews& arguments() const noexcept;

   private:
    void set_arguments(int argc, const char* argv[]) noexcept;

    StringViews arguments_;
#if defined(_WIN32)
    Strings utf8_arguments;
#endif
};

#if defined(_WIN32)
// Get the error message string, if any
std::string error_to_string(DWORD errorId) noexcept;

inline constexpr HANDLE HANDLE_INVALID = nullptr;

[[nodiscard]] constexpr bool is_valid_handle(const HANDLE handle) noexcept {
    return handle != HANDLE_INVALID && handle != INVALID_HANDLE_VALUE;
}

inline constexpr void* MMAP_PTR_INVALID = nullptr;

struct HandleGuard final {
   public:
    explicit HandleGuard(HANDLE& handleRef) noexcept;

    HandleGuard() noexcept = delete;

    HandleGuard(const HandleGuard&) noexcept            = delete;
    HandleGuard& operator=(const HandleGuard&) noexcept = delete;

    HandleGuard(HandleGuard&&) noexcept            = delete;
    HandleGuard& operator=(HandleGuard&&) noexcept = delete;

    ~HandleGuard() noexcept;

    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] HANDLE get() const noexcept { return handle; }

    void reset(HANDLE newHandle = HANDLE_INVALID) noexcept;

    void dismiss() noexcept;

   private:
    HANDLE& handle;
};

struct MMapGuard final {
   public:
    explicit MMapGuard(void*& ptrRef) noexcept;

    MMapGuard() noexcept = delete;

    MMapGuard(const MMapGuard&) noexcept            = delete;
    MMapGuard& operator=(const MMapGuard&) noexcept = delete;

    MMapGuard(MMapGuard&&) noexcept            = delete;
    MMapGuard& operator=(MMapGuard&&) noexcept = delete;

    ~MMapGuard() noexcept;

    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] void* get() const noexcept;

    void reset(void* newPtr = MMAP_PTR_INVALID) noexcept;

    void dismiss() noexcept;

   private:
    void*& mappedPtr;
};

    #if defined(_WIN64)
struct Advapi final {
   public:
    // clang-format off
    // https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openprocesstoken
    using OpenProcessToken_ = BOOL(WINAPI*)(
      HANDLE  ProcessHandle,    // [in]  Handle to process
      DWORD   DesiredAccess,    // [in]  Access rights for token
      PHANDLE TokenHandle       // [out] Pointer to token handle
    );
    // https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-lookupprivilegevaluea
    using LookupPrivilegeValue_ = BOOL(WINAPI*)(
      LPCSTR lpSystemName,      // [in]  System name (NULL for local)
      LPCSTR lpName,            // [in]  Privilege name (e.g., SE_DEBUG_NAME)
      PLUID  lpLuid             // [out] Receives LUID of privilege
    );
    // https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-adjusttokenprivileges
    using AdjustTokenPrivileges_ = BOOL(WINAPI*)(
      HANDLE            TokenHandle,          // [in]       Access token handle
      BOOL              DisableAllPrivileges, // [in]       Disable all privileges flag
      PTOKEN_PRIVILEGES NewState,             // [in, opt]  New privilege state
      DWORD             BufferLength,         // [in]       Size of PreviousState buffer
      PTOKEN_PRIVILEGES PreviousState,        // [out, opt] Previous privilege state
      PDWORD            ReturnLength          // [out, opt] Required buffer size
    );
    // clang-format on

    static constexpr LPCSTR ModuleName = TEXT("advapi32.dll");

    ~Advapi() noexcept;

    bool load() noexcept;

    void free() noexcept;

    OpenProcessToken_      openProcessToken      = nullptr;
    LookupPrivilegeValue_  lookupPrivilegeValue  = nullptr;
    AdjustTokenPrivileges_ adjustTokenPrivileges = nullptr;

   private:
    HMODULE hModule = nullptr;
    bool    loaded  = false;
};
    #endif

template<typename SuccessFunc, typename FailureFunc>
auto try_with_windows_lock_memory_privilege([[maybe_unused]] SuccessFunc&& successFunc,
                                            FailureFunc&&                  failureFunc) noexcept {
    #if defined(_WIN64)
    const SIZE_T largePageSize = GetLargePageMinimum();

    if (largePageSize == 0)
        return failureFunc();

    assert(is_power_of_2(largePageSize));

    Advapi advapi;

    if (!advapi.load())
        return failureFunc();

    HANDLE processHandle = HANDLE_INVALID;

    HandleGuard processHandleGuard{processHandle};

    // Need SeLockMemoryPrivilege, so try to enable it for the process
    if (!advapi.openProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                                 &processHandle))
        return failureFunc();

    TOKEN_PRIVILEGES newTp{};
    newTp.PrivilegeCount           = 1;
    newTp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    // Get the luid
    if (!advapi.lookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME, &newTp.Privileges[0].Luid))
        return failureFunc();

    TOKEN_PRIVILEGES oldTp{};
    DWORD            oldTpLen = 0;

    // Try to enable SeLockMemoryPrivilege. Note that even if AdjustTokenPrivileges() succeeds,
    // Still need to query GetLastError() to ensure that the privileges were actually obtained.
    SetLastError(ERROR_SUCCESS);

    if (!advapi.adjustTokenPrivileges(processHandle, FALSE, &newTp, sizeof(oldTp), &oldTp,
                                      &oldTpLen)
        || GetLastError() != ERROR_SUCCESS)
        return failureFunc();

    // Call the provided function with the privilege enabled
    auto&& ret = successFunc(largePageSize);

    // Privilege no longer needed, restore the privileges
    advapi.adjustTokenPrivileges(processHandle, FALSE, &oldTp, 0, nullptr, nullptr);

    return std::forward<decltype(ret)>(ret);
    #else
    return failureFunc();
    #endif
}

#else
inline constexpr int FD_INVALID = -1;

[[nodiscard]] constexpr bool is_valid_fd(const int fd) noexcept { return fd > FD_INVALID; }

inline constexpr void* MMAP_PTR_INVALID  = nullptr;
inline constexpr usize MMAP_SIZE_INVALID = 0;

struct FdGuard final {
   public:
    explicit FdGuard(int& fdRef) noexcept;

    FdGuard() noexcept = delete;

    FdGuard(const FdGuard&) noexcept            = delete;
    FdGuard& operator=(const FdGuard&) noexcept = delete;

    FdGuard(FdGuard&&) noexcept            = delete;
    FdGuard& operator=(FdGuard&&) noexcept = delete;

    ~FdGuard() noexcept;

    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] int get() const noexcept;

    void reset(int newFd = FD_INVALID) noexcept;

    void dismiss() noexcept;

   private:
    int& fd;
};

struct MMapGuard final {
   public:
    MMapGuard(void*& ptrRef, usize& sizeRef) noexcept;

    MMapGuard() noexcept = delete;

    MMapGuard(const MMapGuard&) noexcept            = delete;
    MMapGuard& operator=(const MMapGuard&) noexcept = delete;

    MMapGuard(MMapGuard&&) noexcept            = delete;
    MMapGuard& operator=(MMapGuard&&) noexcept = delete;

    ~MMapGuard() noexcept;

    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] void* get_ptr() const noexcept;

    [[nodiscard]] usize get_size() const noexcept;

    void reset(void* newPtr = MMAP_PTR_INVALID, usize newSize = MMAP_SIZE_INVALID) noexcept;

    void dismiss() noexcept;

   private:
    void*& mappedPtr;
    usize& mappedSize;
};

struct UniqueFd final {
   public:
    explicit UniqueFd(int fdi) noexcept;

    UniqueFd() noexcept = default;

    UniqueFd(const UniqueFd&)            = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& uniqueFd) noexcept;
    UniqueFd& operator=(UniqueFd&& uniqueFd) noexcept;

    ~UniqueFd() noexcept;

    [[nodiscard]] int get() const noexcept;

    [[nodiscard]] bool is_valid() const noexcept;

    [[nodiscard]] explicit operator bool() const noexcept;

    [[nodiscard]] int release() noexcept;

    void reset(int newFd = FD_INVALID) noexcept;

   private:
    int fd = FD_INVALID;
};

#endif

inline std::string lower_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](char ch) noexcept -> char { return lower_case(ch); });
    return str;
}

inline std::string upper_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](char ch) noexcept -> char { return upper_case(ch); });
    return str;
}

inline std::string toggle_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(), [](char ch) noexcept -> char {
        return is_lower(ch) ? upper_case(ch) : is_upper(ch) ? lower_case(ch) : ch;
    });
    return str;
}

inline std::string remove_whitespace(std::string str) noexcept {
    str.erase(
      std::remove_if(str.begin(), str.end(), [](char ch) noexcept -> bool { return is_space(ch); }),
      str.end());
    return str;
}

[[nodiscard]] constexpr bool starts_with(std::string_view sv, std::string_view prefix) noexcept {
    return sv.size() >= prefix.size()  //
        && sv.compare(0, prefix.size(), prefix) == 0;
}

[[nodiscard]] constexpr bool ends_with(std::string_view sv, std::string_view suffix) noexcept {
    return sv.size() >= suffix.size()  //
        && sv.compare(sv.size() - suffix.size(), suffix.size(), suffix) == 0;
}

[[nodiscard]] constexpr bool is_whitespace(std::string_view sv) noexcept {
    return sv.find_first_not_of(WHITE_SPACE) == std::string_view::npos;
}

[[nodiscard]] constexpr std::string_view ltrim(std::string_view sv) noexcept {
    // Find the first non-whitespace character
    auto beg = sv.find_first_not_of(WHITE_SPACE);

    if (beg == std::string_view::npos)
        return {};

    return sv.substr(beg);
}

[[nodiscard]] constexpr std::string_view rtrim(std::string_view sv) noexcept {
    // Find the last non-whitespace character
    auto end = sv.find_last_not_of(WHITE_SPACE);

    if (end == std::string_view::npos)
        return {};

    return sv.substr(0, end + 1);
}

[[nodiscard]] constexpr std::string_view trim(std::string_view sv) noexcept {
    auto beg = sv.find_first_not_of(WHITE_SPACE);

    if (beg == std::string_view::npos)
        return {};

    auto end = sv.find_last_not_of(WHITE_SPACE);

    return sv.substr(beg, end - beg + 1);
}

[[nodiscard]] constexpr std::string_view bool_to_string(bool b) noexcept {
    return b ? "true" : "false";
}

[[nodiscard]] constexpr bool sv_to_bool(const std::string_view sv) {
    return (trim(sv) == bool_to_string(true));
}

[[nodiscard]] constexpr int sv_to_int(std::string_view sv) noexcept {
    const char* p   = sv.data();
    const char* end = p + sv.size();

    bool neg      = false;
    int  intValue = 0;

    for (; p != end && *p == '-'; ++p)
        neg = true;
    for (; p != end; ++p)
        intValue = 10 * intValue + char_to_digit(*p);

    return neg ? -intValue : intValue;
}

// Validate boolean string (case-insensitive)
inline bool value_is_bool_string(std::string value) noexcept {
    // Convert to lowercase for case-insensitive comparison
    value = lower_case(value);
    return value == bool_to_string(true) || value == bool_to_string(false);
}

inline bool value_in_range(std::string_view sv, int minValue, int maxValue) noexcept {
    const char* p   = sv.data();
    const char* end = p + sv.size();
    // Skip spaces
    for (; p != end && is_space(*p); ++p)
    {}

    int intValue = 0;
    // Parse decimal value (base 10) from string_view
    auto [ptr, ec] = std::from_chars(p, end, intValue, 10);
    if (ec != std::errc{} || ptr != end)
        return false;
    // Check value is in range
    return minValue <= intValue && intValue <= maxValue;
}

inline StringViews
split(std::string_view sv, std::string_view delimiter, bool trimPart = false) noexcept {
    StringViews parts;

    if (sv.empty() || delimiter.empty())
        return parts;  // Avoid infinite loop for empty delimiter

    std::string_view part;

    usize offset = 0;

    while (true)
    {
        auto end = sv.find(delimiter, offset);

        if (end == std::string_view::npos)
            break;

        part = sv.substr(offset, end - offset);

        if (trimPart)
            part = trim(part);

        parts.emplace_back(part);
        offset = end + delimiter.size();
    }

    // Last part
    part = sv.substr(offset);

    if (trimPart)
        part = trim(part);

    parts.emplace_back(part);

    return parts;
}

inline std::string hash_to_string(u64 hash) noexcept {
    constexpr usize BufferSize = HEX64_SIZE + 1;  // 16 hex + '\0'

    Array<char, BufferSize> buffer{};

    int   writtenSize = std::snprintf(buffer.data(), buffer.size(), "%016" PRIX64, hash);
    usize copiedSize  = writtenSize > 0  //
                        ? std::min<usize>(writtenSize, buffer.size() - 1)
                        : 0;

    return std::string{buffer.data(), copiedSize};
}

std::string u32_to_string(u32 v) noexcept;
std::string u64_to_string(u64 v) noexcept;

inline bool InfoStrStop = false;

void print_info_string(std::string_view infos) noexcept;

[[noreturn]] void terminate_on_critical_error(std::string_view message) noexcept;

std::string           utf8_from_wstring(std::wstring_view wsv) noexcept;
std::filesystem::path path_from_utf8(std::string_view path) noexcept;

std::optional<usize> str_to_usize(std::string_view sv) noexcept;

std::optional<std::string> read_file_to_string(const std::filesystem::path& filePath) noexcept;

}  // namespace DON

#endif  // MISC_H_INCLUDED
