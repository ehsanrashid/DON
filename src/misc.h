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
#include <cctype>
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
#include <new>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #include "platform_win.h"
#else
    #include <sys/mman.h>
    #include <unistd.h>
#endif

#if defined(USE_PREFETCH) \
  && ((defined(__x86_64__) || defined(_M_X64)) || (defined(__i386__) || defined(_M_IX86)))
    #include <xmmintrin.h>  // SSE header for _mm_prefetch() intrinsics
    #define HAS_X86_PREFETCH
#endif

#define STRING_LITERAL(x) #x
#define STRINGIFY(x) STRING_LITERAL(x)

#if defined(__clang__)
    #define ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(__GNUC__)
    #define ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
    #define ALWAYS_INLINE __forceinline
#else
    // fallback: keep 'inline'
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
    // fallback: do nothing
    #define ASSUME(cond) ((void) 0)
#endif

#if defined(__clang__)
    #define UNREACHABLE() do { __builtin_unreachable(); } while (false)
#elif defined(__GNUC__)
    #define UNREACHABLE() do { __builtin_unreachable(); } while (false)
#elif defined(_MSC_VER)
    #define UNREACHABLE() __assume(false)
#else
    #define UNREACHABLE() do { } while (false)
#endif
// clang-format on

#if defined(__clang__)
    #define RESTRICT __restrict__
#elif defined(__GNUC__)
    #define RESTRICT __restrict__
#elif defined(_MSC_VER)
    #define RESTRICT __restrict
#else
    // fallback: no restrict
    #define RESTRICT
#endif

#if !defined(NDEBUG)
    #define DEBUG_LOG(msg) std::cerr << msg << std::endl
#else
    #define DEBUG_LOG(msg) ((void) 0)
#endif

namespace DON {

using u64   = std::uint64_t;
using u32   = std::uint32_t;
using u16   = std::uint16_t;
using u8    = std::uint8_t;
using uchar = unsigned char;

using i64   = std::int64_t;
using i32   = std::int32_t;
using i16   = std::int16_t;
using i8    = std::int8_t;
using ichar = char;

using usize = std::size_t;
using isize = std::ptrdiff_t;

#if defined(IS_64BIT) && defined(__GNUC__)
__extension__ using u128 = unsigned __int128;
__extension__ using i128 = signed __int128;
#endif

using Strings     = std::vector<std::string>;
using StringViews = std::vector<std::string_view>;

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
inline constexpr bool IsLittleEndian = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;
#elif defined(_WIN32)
inline constexpr bool IsLittleEndian = true;
#else
// Fallback runtime check
inline const bool IsLittleEndian = []() noexcept {
    constexpr u16 LE = 1;
    return *reinterpret_cast<const u8*>(&LE) == 1;
}();
#endif

constexpr u64 bit(u8 b) noexcept { return (u64{1} << b); }

template<typename To, typename From>
constexpr bool is_strictly_assignable_v =
  std::is_assignable_v<To&, From> && (std::is_same_v<To, From> || !std::is_convertible_v<From, To>);

// Return the sign of a number (-1, 0, +1)
template<
  typename T,
  std::enable_if_t<std::is_arithmetic_v<T> || (std::is_enum_v<T> && std::is_convertible_v<T, int>),
                   int> = 0>
constexpr int sign(T x) noexcept {
    // NaN -> 0; unsigned types never return -1
    return (T(0) < x) - (x < T(0));  // Returns 1 for positive, -1 for negative, and 0 for zero
}

// Return the square of a number, using a wider type to avoid overflow
template<typename T>
constexpr auto sqr(T x) noexcept {
    static_assert(std::is_arithmetic_v<T>, "Argument must be arithmetic");
    using Wider = std::conditional_t<std::is_integral_v<T>, long long, T>;
    return Wider(x) * Wider(x);
}

// Return the square of a number multiplied by its sign, using a wider type to avoid overflow
template<typename T>
constexpr auto sign_sqr(T x) noexcept {
    static_assert(std::is_arithmetic_v<T>, "Argument must be arithmetic");
    return sign(x) * sqr(x);
}

template<typename T>
constexpr bool is_power_of_2(const T x) noexcept {
    return x != 0 && (x & (x - 1)) == 0;
}

template<typename T1, typename T2>
constexpr std::common_type_t<T1, T2> ceil_div(T1 n, T2 d) noexcept {
    using R = std::common_type_t<T1, T2>;
    return (R(n) + R(d) - 1) / R(d);
}

// Round n up to be a multiple of base
template<typename T>
constexpr T ceil_to_multiple(T n, T base) noexcept {
    return ceil_div(n, base) * base;
}

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

// Computes ln(1 + f) for f in (-1, sqrt(2)-1] via the identity
//   ln(1+f) = 2 * atanh(s),   s = f / (2 + f)
//
// After the sqrt(2) range reduction below, |s| <= (sqrt(2)-1)/(sqrt(2)+1)
// = 3 - 2*sqrt(2) ≈ 0.1716, so the series needs only ~10 terms for full
// double precision (truncation error < 2e-17).
constexpr double constexpr_log1p_log(double f) noexcept {
    double s  = f / (2.0 + f);
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
    return constexpr_log1p_log(x - 1.0) + double(exponent) * LN2;
}

constexpr float max_load_factor(float maxLoadFactor = 0.75f) noexcept {
    return std::clamp(constexpr_abs(maxLoadFactor), 0.1f, 1.0f);
}
constexpr usize reserve_count(usize reserveCount = 1024) noexcept {
    return std::max(reserveCount, usize(8));
}

template<typename T1, typename T2>
constexpr T2 interpolate(T1 x, T1 x0, T1 x1, T2 y0, T2 y1) noexcept {
    assert(x0 != x1);
    return T2(y0 + (y1 - y0) * (x - x0) / (x1 - x0));
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

[[nodiscard]] constexpr char digit_to_char(int digit) noexcept {
    assert(0 <= digit && digit <= 9 && "digit_to_char: non-digit integer");

    return 0 <= digit && digit <= 9 ? digit + '0' : '\0';
}

[[nodiscard]] constexpr int char_to_digit(char ch) noexcept {
    assert('0' <= ch && ch <= '9' && "char_to_digit: non-digit character");

    return '0' <= ch && ch <= '9' ? ch - '0' : 0;
}

constexpr std::string_view timestamp() noexcept { return __TIMESTAMP__; }

constexpr char to_lower(const char ch) noexcept {
    return 'A' <= ch && ch <= 'Z' ? char(ch + ('a' - 'A')) : ch;
}
constexpr char to_upper(const char ch) noexcept {
    return 'a' <= ch && ch <= 'z' ? char(ch - ('a' - 'A')) : ch;
}

constexpr unsigned to_month(std::string_view m) noexcept {
    assert(m.size() == 3);
    return to_lower(m[0]) == 'j' && to_lower(m[1]) == 'a' ? 1
         : to_lower(m[0]) == 'f'                          ? 2
         : to_lower(m[0]) == 'm' && to_lower(m[2]) == 'r' ? 3
         : to_lower(m[0]) == 'a' && to_lower(m[1]) == 'p' ? 4
         : to_lower(m[0]) == 'm' && to_lower(m[2]) == 'y' ? 5
         : to_lower(m[0]) == 'j' && to_lower(m[2]) == 'n' ? 6
         : to_lower(m[0]) == 'j' && to_lower(m[2]) == 'l' ? 7
         : to_lower(m[0]) == 'a' && to_lower(m[1]) == 'u' ? 8
         : to_lower(m[0]) == 's'                          ? 9
         : to_lower(m[0]) == 'o'                          ? 10
         : to_lower(m[0]) == 'n'                          ? 11
         : to_lower(m[0]) == 'd'                          ? 12
                                                          : 0;
}

std::string engine_info(bool uci = false) noexcept;

void show_logo() noexcept;

std::string version_info() noexcept;

std::string compiler_info() noexcept;

constexpr u64 mul_hi64(u64 u1, u64 u2) noexcept {
#if defined(IS_64BIT) && defined(__GNUC__) && !defined(__wasm__)
    return (u128(u1) * u128(u2)) >> 64;
#else
    u64 u1L = u32(u1), u1H = u1 >> 32;
    u64 u2L = u32(u2), u2H = u2 >> 32;
    u64 mid = u1H * u2L + ((u1L * u2L) >> 32);
    return u1H * u2H + ((u1L * u2H + u32(mid)) >> 32) + (mid >> 32);
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
    #if defined(HAS_X86_PREFETCH)
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

using TimePoint = std::chrono::milliseconds::rep;  // A value in milliseconds
static_assert(sizeof(TimePoint) == sizeof(i64), "TimePoint size must be 8 bytes");

inline TimePoint now() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string format_time(const std::chrono::system_clock::time_point& timePoint) noexcept;

struct IndexRange final {
   public:
    usize beg;
    usize end;
};

constexpr IndexRange split_range(usize id, usize parts, usize size) noexcept {
    assert(parts != 0 && id < parts);

    usize base  = size / parts;
    usize extra = size % parts;  // remainder to distribute

    // Distribute remainder among the first 'extra' threads
    usize beg = id * base + std::min(id, extra);
    usize end = beg + base + usize(id < extra);

    assert(beg <= end && end <= size);
    return {beg, end};
}

struct CallOnce final {
   public:
    CallOnce() noexcept                           = default;
    CallOnce(const CallOnce&) noexcept            = delete;
    CallOnce& operator=(const CallOnce&) noexcept = delete;
    CallOnce(CallOnce&&) noexcept                 = delete;
    CallOnce& operator=(CallOnce&&) noexcept      = delete;

    // Initialize using the provided function
    // The function will be called exactly once, even if multiple threads call this
    template<typename Func>
    void operator()(Func&& callFn) noexcept(noexcept(callFn())) {
        std::call_once(callOnce, [this, callFunc = std::forward<Func>(callFn)]() mutable {
            std::move(callFunc)();  // Move into the call
            initialize.store(true, std::memory_order_release);
        });
    }

    // Check if initialization has been completed
    [[nodiscard]] bool initialized() const noexcept {
        return initialize.load(std::memory_order_acquire);
    }

   private:
    std::once_flag    callOnce;
    std::atomic<bool> initialize{false};
};

// LazyValue wraps a Value with CallOnce for safe lazy initialization
template<typename Value>
struct LazyValue final {
   public:
    LazyValue() noexcept                            = default;
    LazyValue(const LazyValue&) noexcept            = delete;
    LazyValue& operator=(const LazyValue&) noexcept = delete;
    LazyValue(LazyValue&&) noexcept                 = delete;
    LazyValue& operator=(LazyValue&&) noexcept      = delete;

    ~LazyValue() noexcept {
        if (initialized())
            get_ptr()->~Value();
    }

    template<typename... Args>
    Value& init(Args&&... args) noexcept(std::is_nothrow_constructible_v<Value, Args...>) {
        // Fast path: already initialized
        if (initialized())
            return *get_ptr();

        // Initialize exactly once, use tuple to capture all arguments
        callOnce([this, tuple = std::make_tuple(std::forward<Args>(args)...)]() mutable {
            std::apply(
              [this](auto&&... captured) {
                  new (get_ptr()) Value(std::forward<decltype(captured)>(captured)...);
              },
              std::move(tuple));
        });

        return *get_ptr();
    }

    Value& get() noexcept {
        assert(initialized() && "LazyValue accessed before initialization");
        return *get_ptr();
    }

    const Value& get() const noexcept {
        assert(initialized() && "LazyValue accessed before initialization");
        return *get_ptr();
    }

    [[nodiscard]] bool initialized() const noexcept { return callOnce.initialized(); }

   private:
    Value* get_ptr() noexcept { return std::launder(reinterpret_cast<Value*>(&storage)); }

    const Value* get_ptr() const noexcept {
        return std::launder(reinterpret_cast<const Value*>(&storage));
    }

    alignas(Value) std::byte storage[sizeof(Value)];
    CallOnce callOnce;
};

// OstreamMutexRegistry
//
// A thread-safe registry that provides a unique mutex for each std::ostream pointer.
// This is useful when multiple threads may write to the same ostream and you want
// to synchronize access without locking unrelated streams.
//
// Key Features:
//  - Thread-safe: internal access to the registry map is protected by a mutex.
//  - Per-ostream mutex: each ostream gets its own mutex to avoid contention.
//  - Null-safe: passing a nullptr returns a null-mutex to safely ignore locking
//    without inserting invalid keys into the map.
//  - Lazy initialization: mutexes are default-constructed when first requested.
//
// Usage:
//  - Call 'get(&std::cout)' to obtain a mutex before writing to std::cout from multiple threads.
//  - Lock the returned mutex with std::scoped_lock or std::unique_lock.
//
// Notes:
//  - The class is static-only; it cannot be instantiated. (Restriction)
//  - Mutexes are stored as object in the map.
class OstreamMutexRegistry final {
   public:
    static void ensure_initialized(usize reserveCount = 16, float maxLoadFactor = 0.85f) noexcept {
        callOnce([reserveCount, maxLoadFactor]() noexcept {
            osMutexes.max_load_factor(max_load_factor(maxLoadFactor));
            osMutexes.reserve(reserve_count(reserveCount));
        });
    }

    // Return a mutex associated with the given ostream pointer.
    // If ptrOs is nullptr, returns a null-mutex to safely ignore locking.
    // This ensures no accidental insertion of null keys into the map.
    static std::mutex& get(std::ostream* ptrOs) noexcept {
        ensure_initialized();

        // Fallback for null pointers
        if (ptrOs == nullptr)
            return nullMutex;

        // Lock the registry while accessing the map
        std::lock_guard writeLock(mutex);

        // Return mutex, create if missing
        return osMutexes[ptrOs];
    }

   private:
    OstreamMutexRegistry() noexcept                                       = delete;
    ~OstreamMutexRegistry() noexcept                                      = delete;
    OstreamMutexRegistry(const OstreamMutexRegistry&) noexcept            = delete;
    OstreamMutexRegistry& operator=(const OstreamMutexRegistry&) noexcept = delete;
    OstreamMutexRegistry(OstreamMutexRegistry&&) noexcept                 = delete;
    OstreamMutexRegistry& operator=(OstreamMutexRegistry&&) noexcept      = delete;

    static inline CallOnce callOnce;
    // Protects access to the osMutexes map for thread safety
    static inline std::mutex mutex;
    // Note: null-mutex shared by all nullptr streams
    static inline std::mutex nullMutex;
    // Store mutexes and references returned by get()
    static inline std::unordered_map<std::ostream*, std::mutex> osMutexes;
};

// SyncOstream --- Synchronized output stream ---
//
// A RAII-style wrapper for synchronizing output to a std::ostream across multiple threads.
// Each SyncOstream locks a mutex associated with the given ostream (via OstreamMutexRegistry)
// during its lifetime, ensuring thread-safe writes.
//
// Key Features:
//  - Thread-safe: locks the ostream-specific mutex for the duration of the SyncOstream object.
//  - RAII-based: mutex is automatically locked on construction and released on destruction.
//  - Move-constructible: can be returned from factories or functions by value.
//  - Deleted copy and move-assignment: prevents accidental unlocking windows or double-locks.
//  - Supports all standard ostream operators and manipulators (std::endl, std::flush, etc.).
//  - Asserts on use of moved-from SyncOstream to catch logic errors in debug builds.
//
// Usage Example:
//   SyncOstream(syncOut) << "Thread-safe message " << value << std::endl;
//   where syncOut is a std::ostream (like std::cout or a file stream)
//   that you want to write to safely from multiple threads.
//
// Notes:
//  - Designed for short-lived, scoped output operations; lock is held for the lifetime
//    of the SyncOstream object.
//  - Uses OstreamMutexRegistry internally to avoid creating multiple mutexes for the same ostream.
class [[nodiscard]] SyncOstream final {
   public:
    explicit SyncOstream(std::ostream& os) noexcept :
        ptrOs(&os),
        lock(OstreamMutexRegistry::get(ptrOs)) {}
    // Move-constructible so factories can return by value
    SyncOstream(SyncOstream&& syncOs) noexcept :
        ptrOs(syncOs.ptrOs),
        lock(std::move(syncOs.lock)) {}

    SyncOstream(const SyncOstream&) noexcept            = delete;
    SyncOstream& operator=(const SyncOstream&) noexcept = delete;
    // Prefer deleting move-assignment to avoid unlock window
    SyncOstream& operator=(SyncOstream&&) noexcept = delete;

    template<typename T>
    SyncOstream& operator<<(T&& x) & {
        assert(ptrOs != nullptr && "Use of moved-from SyncOstream");

        *ptrOs << std::forward<T>(x);
        return *this;
    }
    template<typename T>
    SyncOstream&& operator<<(T&& x) && {
        assert(ptrOs != nullptr && "Use of moved-from SyncOstream");

        *ptrOs << std::forward<T>(x);
        return std::move(*this);
    }

    using IosManipulator = std::ios& (*) (std::ios&);

    SyncOstream& operator<<(IosManipulator manip) & {
        assert(ptrOs != nullptr && "Use of moved-from SyncOstream");

        manip(*ptrOs);
        return *this;
    }
    SyncOstream&& operator<<(IosManipulator manip) && {
        assert(ptrOs != nullptr && "Use of moved-from SyncOstream");

        manip(*ptrOs);
        return std::move(*this);
    }

    using OstreamManipulator = std::ostream& (*) (std::ostream&);

    SyncOstream& operator<<(OstreamManipulator manip) & {
        assert(ptrOs != nullptr && "Use of moved-from SyncOstream");

        manip(*ptrOs);
        return *this;
    }
    SyncOstream&& operator<<(OstreamManipulator manip) && {
        assert(ptrOs != nullptr && "Use of moved-from SyncOstream");

        manip(*ptrOs);
        return std::move(*this);
    }

   private:
    std::ostream* const          ptrOs;
    std::unique_lock<std::mutex> lock;
};

[[nodiscard]] inline SyncOstream sync_os(std::ostream& os = std::cout) noexcept {
    return SyncOstream(os);
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

    constexpr T& operator[](usize idx) noexcept {
        assert(idx < size());
        return data()[idx];
    }
    constexpr T& operator[](usize idx) const noexcept {
        assert(idx < size());
        return data()[idx];
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

    constexpr const auto& at(size_type idx) const { return data_.at(idx); }
    constexpr auto&       at(size_type idx) { return data_.at(idx); }

    constexpr const auto& operator[](size_type idx) const noexcept {
        assert(idx < size());

        return data_[idx];
    }
    constexpr auto& operator[](size_type idx) noexcept {
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
    void fill_n(usize beg, usize count, const U& v) noexcept {
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
    //    std::cout << Size << ':' << sizeof...(Sizes) << std::endl;
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

        *end() = value;  // copy-assign into pre-initialized slot
        ++size_;
    }
    void push_back(T&& value) noexcept {
        assert(size() < capacity());

        *end() = std::move(value);
        ++size_;
    }
    template<typename... Args>
    void emplace_back(Args&&... args) noexcept {
        assert(size() < capacity());

        *end() = T(std::forward<Args>(args)...);
        ++size_;
    }

    // Append value if value < max
    void push_back_if_lt(const T& value, const T& maxValue) noexcept {
        assert(size() < capacity());

        *end() = value;
        size_ += usize(value < maxValue);
    }

    void pop_back() noexcept {
        assert(size() != 0);

        --size_;
    }

    T& back() noexcept {
        assert(size() > 0);

        return data()[size() - 1];
    }
    const T& back() const noexcept {
        assert(size() > 0);

        return data()[size() - 1];
    }

    T& operator[](SizeType idx) noexcept {
        assert(idx < size());

        return data()[idx];
    }
    const T& operator[](SizeType idx) const noexcept {
        assert(idx < size());

        return data()[idx];
    }

    void resize(SizeType newSize) noexcept {
        // Note: doesn't construct/destroy elements
        size_ = std::min(newSize, capacity());
    }

    T* make_space(SizeType space) noexcept {
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
    // from_view factory
    static FixedText from_view(std::string_view sv) noexcept { return FixedText{}.write(sv); }

    FixedText& write(char ch) noexcept {
        assert(size() < capacity());
        if (size() >= capacity())
            return *this;
        *end() = ch;
        ++size_;
        return *this;
    }

    FixedText& write(std::string_view sv) noexcept {
        assert(size() + sv.size() <= capacity());
        std::memcpy(end(), sv.data(), sv.size());
        size_ += sv.size();
        return *this;
    }

    FixedText& write(int v) noexcept {
        auto [ptr, ec] = std::to_chars(end(), begin() + capacity(), v);
        assert(ec == std::errc{});
        size_ = ptr - begin();
        return *this;
    }

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

    friend std::ostream& operator<<(std::ostream& os, const FixedText& fixedText) noexcept;

   private:
    Array<char, 31> data_{};
    u8              size_ = 0;
};

static_assert(sizeof(FixedText) == 32, "FixedText size must be 32 bytes");

// ConcurrentCache: groups (mutex + storage + pre-reserve)
template<typename Key, typename Value>
class ConcurrentCache final {
   public:
    explicit ConcurrentCache(usize reserveCount = 1024, float maxLoadFactor = 0.75f) noexcept {
        storage.max_load_factor(max_load_factor(maxLoadFactor));
        storage.reserve(reserve_count(reserveCount));
    }

    template<typename... Args>
    Value& access_or_build(const Key& key, Args&&... args) noexcept {
        // Fast path: shared read lock to check and access
        {
            std::shared_lock readLock(sharedMutex);

            auto itr = storage.find(key);

            if (itr != storage.end())
                return get_value(itr->second);
        }

        // Slow path: exclusive write lock to insert and construct
        std::lock_guard writeLock(sharedMutex);

        // Double-check after acquiring exclusive lock
        auto [itr, inserted] = storage.try_emplace(key);

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
    static constexpr usize THRESHOLD_SIZE = 128;

    // Define StorageValue type alias
    using StorageValue =
      std::conditional_t<sizeof(Value) <= THRESHOLD_SIZE, Value, std::unique_ptr<Value>>;

    // Helper functions AFTER StorageValue is defined
    template<typename... Args>
    void set_value(StorageValue& entry, Args&&... args) {
        if constexpr (sizeof(Value) <= THRESHOLD_SIZE)
            entry = Value(std::forward<Args>(args)...);
        else
            entry = std::make_unique<Value>(std::forward<Args>(args)...);
    }

    static Value& get_value(StorageValue& entry) noexcept {
        if constexpr (sizeof(Value) <= THRESHOLD_SIZE)
            return entry;
        else
            return *entry;
    }

    std::shared_mutex                     sharedMutex;
    std::unordered_map<Key, StorageValue> storage;
};

// Hash function based on public domain MurmurHash64A by Austin Appleby.
// Fast, non-cryptographic 64-bit hash suitable for general-purpose hashing.
inline u64 hash_bytes(const char* RESTRICT data, usize size, u64 seed = 0) noexcept {
    constexpr u64 MurmurM = u64{0xC6A4A7935BD1E995};
    constexpr u8  MurmurR = 47;

    // Mix 64-bit block (MurmurHash64A core mixing step)
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
        u64 k0, k1, k2, k3;
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
        u64 k0, k1;
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
        u64 k;
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
            k |= u64(*p) << shift;

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
class StringViewStreamBuf final: public std::streambuf {
   public:
    explicit StringViewStreamBuf(std::string_view sv) noexcept {
        // std::streambuf requires char* for the get area.
        // The buffer is read-only; no characters are modified.
        auto* p = const_cast<char*>(sv.data());
        setg(p, p, p + sv.size());  // Only GET area (reading enabled)
        // Do NOT call setp(p, p + sv.size()) - no PUT area (writing disabled)
    }
};

// Custom streambuf that wraps memory stream
class MemoryStreamBuf final: public std::streambuf {
   public:
    MemoryStreamBuf(char* p, usize size) noexcept {
        setg(p, p, p + size);  // Only GET area (reading enabled)
        // Do NOT call setp(p, p + size) - no PUT area (writing disabled)
    }
};

// Fancy logging facility.
// The trick here is to replace cin.rdbuf() and cout.rdbuf() with 2 TieStreamBuf objects
// that tie std::cin and std::cout to a file stream.
// Can toggle the logging of std::cout and std::cin at runtime whilst preserving
// usual I/O functionality, all without changing a single line of code!
// Idea from http://groups.google.com/group/comp.lang.c++/msg/1d941c0f26ea0d81
// MSVC requires split streambuf for std::cin and std::cout.
class TieStreamBuf final: public std::streambuf {
   public:
    using traits_type = std::streambuf::traits_type;
    using int_type    = traits_type::int_type;
    using char_type   = traits_type::char_type;

    TieStreamBuf() noexcept = delete;
    TieStreamBuf(std::streambuf* pB, std::streambuf* mB) noexcept :
        pBuf(pB),
        mBuf(mB) {}

    int_type overflow(int_type ch) override {
        if (pBuf == nullptr)
            return traits_type::eof();

        if (traits_type::eq_int_type(ch, traits_type::eof()))
            return traits_type::not_eof(ch);

        int_type putCh = pBuf->sputc(traits_type::to_char_type(ch));

        if (traits_type::eq_int_type(putCh, traits_type::eof()))
            return putCh;

        return mirror_put_with_prefix(putCh, "<< ", preOutCh);
    }

    int_type underflow() override {
        if (pBuf == nullptr)
            return traits_type::eof();

        return pBuf->sgetc();
    }

    int_type uflow() override {
        if (pBuf == nullptr)
            return traits_type::eof();

        int_type ch = pBuf->sbumpc();

        if (traits_type::eq_int_type(ch, traits_type::eof()))
            return ch;

        return mirror_put_with_prefix(ch, ">> ", preInCh);
    }

    int sync() override {
        int r1 = pBuf != nullptr ? pBuf->pubsync() : 0;
        int r2 = mBuf != nullptr ? mBuf->pubsync() : 0;

        return (r1 == 0 && r2 == 0) ? 0 : -1;
    }

    std::streamsize xsputn(const char_type* s, std::streamsize count) override {
        if (pBuf == nullptr)
            return 0;

        std::streamsize written = pBuf->sputn(s, count);

        if (mBuf != nullptr && written > 0)
        {
            // Prefix injection only once if needed
            if (preOutCh == '\n')
                mBuf->sputn("<< ", 3);

            mBuf->sputn(s, written);

            preOutCh = s[written - 1];  // track last char
        }

        return written;
    }

    [[nodiscard]] std::streambuf* pbuf() const { return pBuf; }
    [[nodiscard]] std::streambuf* mbuf() const { return mBuf; }

   private:
    int_type
    mirror_put_with_prefix(int_type ch, std::string_view prefix, char_type& preCh) noexcept {
        if (mBuf == nullptr)
            return traits_type::not_eof(ch);

        if (preCh == '\n')
            mBuf->sputn(prefix.data(), std::streamsize(prefix.size()));

        char_type c = traits_type::to_char_type(ch);
        preCh       = c;

        int_type r = mBuf->sputc(c);
        return traits_type::eq_int_type(r, traits_type::eof()) ? traits_type::eof()
                                                               : traits_type::not_eof(ch);
    }

    std::streambuf *pBuf, *mBuf;

    char_type preOutCh = '\n';
    char_type preInCh  = '\n';
};

class Logger final {
   public:
    // Start logging. Returns true on success.
    static bool start(const std::filesystem::path& logFile) noexcept {
        std::lock_guard writeLock(instance().mutex);

        return instance().open(logFile);
    }

    // Stop logging. Restores original streams and closes the file.
    static void stop() noexcept {
        std::lock_guard writeLock(instance().mutex);

        instance().close();
    }

   private:
    Logger() noexcept = delete;
    Logger(std::istream& isRef, std::ostream& osRef) noexcept :
        is(isRef),
        os(osRef),
        isBuf(is.rdbuf()),
        osBuf(os.rdbuf()),
        iTie(is.rdbuf(), ofs.rdbuf()),
        oTie(os.rdbuf(), ofs.rdbuf()) {}

    ~Logger() noexcept { close(); }

    // Single shared instance
    static Logger& instance() noexcept {
        // Tie std::cin and std::cout to a file
        static Logger logger(std::cin, std::cout);

        return logger;
    }

    [[nodiscard]] bool is_open() const noexcept { return ofs.is_open(); }

    void write_timestamp(std::string_view suffix) noexcept {
        if (!ofs)
            return;

        ofs << '[' << format_time(std::chrono::system_clock::now()) << "] " << suffix << std::endl;
    }

    // Open log file; caller must hold mutex
    // If another file is already open, it will be closed first.
    bool open(const std::filesystem::path& logFile) noexcept {
        if (filename == logFile.string() && is_open())
            return true;  // Already open

        close();  // Close any previous log file

        if (logFile.empty())
            return true;

        filename = logFile.string();

        ofs.open(filename, std::ios::out | std::ios::app);

        if (!is_open())
        {
            DEBUG_LOG("Unable to open Log file: " << filename);
            return false;
        }

        write_timestamp("->");

        is.rdbuf(&iTie);
        os.rdbuf(&oTie);

        return true;
    }

    // Close log file if open; caller must hold mutex
    void close() noexcept {
        if (!is_open())
            return;

        is.rdbuf(isBuf);
        os.rdbuf(osBuf);

        write_timestamp("<-");

        ofs.close();

        filename.clear();
    }

    std::mutex      mutex;
    std::ofstream   ofs;
    std::istream&   is;
    std::ostream&   os;
    std::streambuf *isBuf = nullptr, *osBuf = nullptr;
    TieStreamBuf    iTie, oTie;
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

    static std::filesystem::path binary_directory(std::filesystem::path path) noexcept;
    static std::filesystem::path working_directory() noexcept;

    StringViews arguments;

   private:
    void set_arguments(int argc, const char* argv[]) noexcept;

#if defined(_WIN32)
    Strings argStorage;
#endif
};

inline std::string lower_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](uchar ch) noexcept -> ichar { return std::tolower(ch); });
    return str;
}

inline std::string upper_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](uchar ch) noexcept -> ichar { return std::toupper(ch); });
    return str;
}

inline std::string toggle_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(), [](uchar ch) noexcept -> ichar {
        return std::islower(ch) ? std::toupper(ch) : std::isupper(ch) ? std::tolower(ch) : ch;
    });
    return str;
}

inline std::string remove_whitespace(std::string str) noexcept {
    str.erase(std::remove_if(str.begin(), str.end(),
                             [](uchar ch) noexcept -> bool { return std::isspace(ch); }),
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
    usize beg = sv.find_first_not_of(WHITE_SPACE);

    if (beg == std::string_view::npos)
        return {};

    return sv.substr(beg);
}

[[nodiscard]] constexpr std::string_view rtrim(std::string_view sv) noexcept {
    // Find the last non-whitespace character
    usize end = sv.find_last_not_of(WHITE_SPACE);

    if (end == std::string_view::npos)
        return {};

    return sv.substr(0, end + 1);
}

[[nodiscard]] constexpr std::string_view trim(std::string_view sv) noexcept {
    usize beg = sv.find_first_not_of(WHITE_SPACE);

    if (beg == std::string_view::npos)
        return {};

    usize end = sv.find_last_not_of(WHITE_SPACE);

    return sv.substr(beg, end - beg + 1);
}

[[nodiscard]] constexpr std::string_view bool_to_string(bool b) noexcept {
    return b ? "true" : "false";
}
// Efficient check: works for std::string or std::string_view
[[nodiscard]] constexpr bool valid_bool_string(std::string_view value) noexcept {
    return value == "true" || value == "false";
}

[[nodiscard]] constexpr bool sv_to_bool(std::string_view sv) { return (trim(sv) == "true"); }

[[nodiscard]] constexpr int sv_to_int(std::string_view sv) noexcept {
    int  intValue = 0;
    bool neg      = false;

    usize i = 0;

    while (i < sv.size() && sv[i] == '-')
    {
        neg = true;
        ++i;
    }

    while (i < sv.size())
    {
        intValue = 10 * intValue + char_to_digit(sv[i]);
        ++i;
    }

    return neg ? -intValue : +intValue;
}

inline std::string clamp_string(std::string_view sv, int minValue, int maxValue) noexcept {
    int intValue = 0;

    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), intValue);

    switch (ec)
    {
    case std::errc::invalid_argument :
        intValue = minValue;
        break;
    case std::errc::result_out_of_range :
        intValue = maxValue;
        break;
    default :;
    }

    return std::to_string(std::clamp(intValue, minValue, maxValue));
}

inline StringViews
split(std::string_view sv, std::string_view delimiter, bool trimPart = false) noexcept {
    StringViews parts;

    if (sv.empty() || delimiter.empty())
        return parts;  // Avoid infinite loop for empty delimiter

    std::string_view part;

    usize beg = 0;

    while (true)
    {
        usize end = sv.find(delimiter, beg);

        if (end == std::string_view::npos)
            break;

        part = sv.substr(beg, end - beg);

        if (trimPart)
            part = trim(part);

        if (!is_whitespace(part))
            parts.emplace_back(part);

        beg = end + delimiter.size();
    }

    // Last part
    part = sv.substr(beg);

    if (trimPart)
        part = trim(part);

    if (!is_whitespace(part))
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

inline std::string u32_to_string(u32 v) noexcept {
    constexpr usize BufferSize = 2 + HEX32_SIZE + 1;  // "0x" + 8 hex + '\0'

    Array<char, BufferSize> buffer{};

    int   writtenSize = std::snprintf(buffer.data(), buffer.size(), "0x%08" PRIX32, v);
    usize copiedSize  = writtenSize > 0  //
                        ? std::min<usize>(writtenSize, buffer.size() - 1)
                        : 0;

    return std::string{buffer.data(), copiedSize};
}
inline std::string u64_to_string(u64 v) noexcept {
    constexpr usize BufferSize = 2 + HEX64_SIZE + 1;  // "0x" + 16 hex + '\0'

    Array<char, BufferSize> buffer{};

    int   writtenSize = std::snprintf(buffer.data(), buffer.size(), "0x%016" PRIX64, v);
    usize copiedSize  = writtenSize > 0  //
                        ? std::min<usize>(writtenSize, buffer.size() - 1)
                        : 0;

    return std::string{buffer.data(), copiedSize};
}

std::string           utf8_from_wstring(std::wstring_view s) noexcept;
std::filesystem::path path_from_utf8(std::string_view path) noexcept;

std::optional<usize> str_to_size_t(std::string_view sv) noexcept;

// Reads the file as bytes.
// Returns std::nullopt if the file does not exist.
std::optional<std::string> read_file_to_string(const std::filesystem::path& filePath) noexcept;

#if defined(_WIN32)
// Get the error message string, if any
inline std::string error_to_string(DWORD errorId) noexcept {
    if (errorId == 0)
        return {};

    LPSTR buffer = nullptr;
    // Ask Win32 to give us the string version of that message ID.
    // The parameters pass in, tell Win32 to create the buffer that holds the message
    // (because don't yet know how long the message string will be).
    usize size = FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, errorId, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&buffer),  // must pass pointer to buffer pointer
      0, nullptr);

    if (size == 0 || buffer == nullptr)
    {
        // FormatMessage failed; return a fallback string
        return "Unknown error: " + u32_to_string(errorId);
    }

    // Copy the error message into a std::string
    std::string message{buffer, size};
    // Trim trailing CR/LF that many system messages include
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n'))
        message.pop_back();
    // Free the Win32's string's buffer
    LocalFree(buffer);

    return message;
}

inline constexpr HANDLE INVALID_HANDLE = nullptr;

[[nodiscard]] constexpr bool is_valid_handle(HANDLE handle) noexcept {
    return handle != INVALID_HANDLE && handle != INVALID_HANDLE_VALUE;
}

inline constexpr void* INVALID_MMAP_PTR = nullptr;

struct HandleGuard final {
   public:
    explicit HandleGuard(HANDLE& refHandle) noexcept :
        handle(refHandle) {}

    HandleGuard() noexcept = delete;

    HandleGuard(const HandleGuard&) noexcept            = delete;
    HandleGuard& operator=(const HandleGuard&) noexcept = delete;

    HandleGuard(HandleGuard&&) noexcept            = delete;
    HandleGuard& operator=(HandleGuard&&) noexcept = delete;

    ~HandleGuard() noexcept { reset(); }

    [[nodiscard]] bool is_valid() const noexcept { return is_valid_handle(handle); }

    [[nodiscard]] HANDLE get() const noexcept { return handle; }

    void reset(HANDLE newHandle = INVALID_HANDLE) noexcept {
        if (handle != newHandle)
        {
            if (is_valid())
                CloseHandle(handle);

            handle = newHandle;
        }
    }

    void dismiss() noexcept { handle = INVALID_HANDLE; }

   private:
    HANDLE& handle;
};

struct MMapGuard final {
   public:
    explicit MMapGuard(void*& refPtr) noexcept :
        mappedPtr(refPtr) {}

    MMapGuard() noexcept = delete;

    MMapGuard(const MMapGuard&) noexcept            = delete;
    MMapGuard& operator=(const MMapGuard&) noexcept = delete;

    MMapGuard(MMapGuard&&) noexcept            = delete;
    MMapGuard& operator=(MMapGuard&&) noexcept = delete;

    ~MMapGuard() noexcept { reset(); }

    [[nodiscard]] bool is_valid() const noexcept { return mappedPtr != INVALID_MMAP_PTR; }

    [[nodiscard]] void* get() const noexcept { return mappedPtr; }

    void reset(void* newPtr = INVALID_MMAP_PTR) noexcept {
        if (mappedPtr != newPtr)
        {
            if (is_valid())
                UnmapViewOfFile(mappedPtr);

            mappedPtr = newPtr;
        }
    }

    void dismiss() noexcept { mappedPtr = INVALID_MMAP_PTR; }

   private:
    void*& mappedPtr;
};

#else

inline constexpr int INVALID_FD = -1;

[[nodiscard]] constexpr bool is_valid_fd(int fd) noexcept { return fd > INVALID_FD; }

inline constexpr void* INVALID_MMAP_PTR  = nullptr;
inline constexpr usize INVALID_MMAP_SIZE = 0;

struct FdGuard final {
   public:
    explicit FdGuard(int& refFd) noexcept :
        fd(refFd) {}

    FdGuard() noexcept = delete;

    FdGuard(const FdGuard&) noexcept            = delete;
    FdGuard& operator=(const FdGuard&) noexcept = delete;

    FdGuard(FdGuard&&) noexcept            = delete;
    FdGuard& operator=(FdGuard&&) noexcept = delete;

    ~FdGuard() noexcept { reset(); }

    [[nodiscard]] bool is_valid() const noexcept { return is_valid_fd(fd); }

    [[nodiscard]] int get() const noexcept { return fd; }

    void reset(int newFd = INVALID_FD) noexcept {
        if (fd != newFd)
        {
            if (is_valid())
                ::close(fd);

            fd = newFd;
        }
    }

    void dismiss() noexcept { fd = INVALID_FD; }

   private:
    int& fd;
};

struct MMapGuard final {
   public:
    MMapGuard(void*& refPtr, usize& refSize) noexcept :
        mappedPtr(refPtr),
        mappedSize(refSize) {}

    MMapGuard() noexcept = delete;

    MMapGuard(const MMapGuard&) noexcept            = delete;
    MMapGuard& operator=(const MMapGuard&) noexcept = delete;

    MMapGuard(MMapGuard&&) noexcept            = delete;
    MMapGuard& operator=(MMapGuard&&) noexcept = delete;

    ~MMapGuard() noexcept { reset(); }

    [[nodiscard]] bool is_valid() const noexcept { return mappedPtr != INVALID_MMAP_PTR; }

    [[nodiscard]] void* get_ptr() const noexcept { return mappedPtr; }

    [[nodiscard]] usize get_size() const noexcept { return mappedSize; }

    void reset(void* newPtr = INVALID_MMAP_PTR, usize newSize = INVALID_MMAP_SIZE) noexcept {
        if (mappedPtr != newPtr)
        {
            if (is_valid())
                ::munmap(mappedPtr, mappedSize);

            mappedPtr  = newPtr;
            mappedSize = newSize;
        }
    }

    void dismiss() noexcept {
        mappedPtr  = INVALID_MMAP_PTR;
        mappedSize = INVALID_MMAP_SIZE;
    }

   private:
    void*& mappedPtr;
    usize& mappedSize;
};

struct UniqueFd final {
   public:
    explicit UniqueFd(int iFd) noexcept :
        fd{iFd} {}

    UniqueFd() noexcept = default;

    UniqueFd(const UniqueFd&)            = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& uniqueFd) noexcept :
        fd{uniqueFd.release()} {}

    UniqueFd& operator=(UniqueFd&& uniqueFd) noexcept {
        if (this == &uniqueFd)
            return *this;

        reset(uniqueFd.release());

        return *this;
    }

    ~UniqueFd() { reset(); }

    [[nodiscard]] int get() const noexcept { return fd; }

    [[nodiscard]] bool is_valid() const noexcept { return is_valid_fd(fd); }

    [[nodiscard]] explicit operator bool() const noexcept { return is_valid(); }

    [[nodiscard]] int release() noexcept { return std::exchange(fd, INVALID_FD); }

    void reset(int newFd = INVALID_FD) noexcept {
        if (fd != newFd)
        {
            if (is_valid())
                ::close(fd);

            fd = newFd;
        }
    }

   private:
    int fd = INVALID_FD;
};

#endif


}  // namespace DON

#endif  // MISC_H_INCLUDED
