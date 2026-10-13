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

#ifndef UTILITY_TYPES_H_INCLUDED
#define UTILITY_TYPES_H_INCLUDED

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#define STRING_LITERAL(x) #x
#define STRINGIFY(x) STRING_LITERAL(x)

// Enable GNU-style compiler attributes for Clang and GCC, excluding SWIG.
// Unsupported compilers and SWIG receive an empty attribute macro.
#if (defined(__clang__) || defined(__GNUC__)) && !defined(SWIG)
    #define ATTRIBUTE(x) __attribute__((x))
#else
    #define ATTRIBUTE(x)
#endif

#if defined(__clang__) || defined(__GNUC__)
    #define ALWAYS_INLINE inline ATTRIBUTE(always_inline)
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
        #define ASSUME(cond) ATTRIBUTE(assume(cond))
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

#if defined(__clang__) || defined(__GNUC__)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define UNLIKELY(x) (x)
#endif

#if !defined(__INTELLISENSE__)
    // Enable compiler thread-safety attributes only for Clang, excluding SWIG.
    // These attributes have no effect when compiling with other compilers.
    #if defined(__clang__) && !defined(SWIG)
        #define THREAD_SAFETY_ATTRIBUTE(x) ATTRIBUTE(x)
    #else
        #define THREAD_SAFETY_ATTRIBUTE(x)
    #endif

    // clang-format off
    #define ASSERT_CAPABILITY(x)        THREAD_SAFETY_ATTRIBUTE(assert_capability(x))
    #define ASSERT_SHARED_CAPABILITY(x) THREAD_SAFETY_ATTRIBUTE(assert_shared_capability(x))
    #define RETURN_CAPABILITY(x)        THREAD_SAFETY_ATTRIBUTE(lock_returned(x))
    #define SCOPED_CAPABILITY           THREAD_SAFETY_ATTRIBUTE(scoped_lockable)
    #define CAPABILITY(x)               THREAD_SAFETY_ATTRIBUTE(capability(x))
    #define GUARDED_BY(x)               THREAD_SAFETY_ATTRIBUTE(guarded_by(x))
    #define PT_GUARDED_BY(x)            THREAD_SAFETY_ATTRIBUTE(pt_guarded_by(x))
    #define ACQUIRED_BEFORE(...)        THREAD_SAFETY_ATTRIBUTE(acquired_before(__VA_ARGS__))
    #define ACQUIRED_AFTER(...)         THREAD_SAFETY_ATTRIBUTE(acquired_after(__VA_ARGS__))
    #define REQUIRES(...)               THREAD_SAFETY_ATTRIBUTE(requires_capability(__VA_ARGS__))
    #define REQUIRES_SHARED(...)        THREAD_SAFETY_ATTRIBUTE(requires_shared_capability(__VA_ARGS__))
    #define ACQUIRE(...)                THREAD_SAFETY_ATTRIBUTE(acquire_capability(__VA_ARGS__))
    #define ACQUIRE_SHARED(...)         THREAD_SAFETY_ATTRIBUTE(acquire_shared_capability(__VA_ARGS__))
    #define RELEASE(...)                THREAD_SAFETY_ATTRIBUTE(release_capability(__VA_ARGS__))
    #define RELEASE_SHARED(...)         THREAD_SAFETY_ATTRIBUTE(release_shared_capability(__VA_ARGS__))
    #define TRY_ACQUIRE(...)            THREAD_SAFETY_ATTRIBUTE(try_acquire_capability(__VA_ARGS__))
    #define TRY_ACQUIRE_SHARED(...)     THREAD_SAFETY_ATTRIBUTE(try_acquire_shared_capability(__VA_ARGS__))
    #define EXCLUDES(...)               THREAD_SAFETY_ATTRIBUTE(locks_excluded(__VA_ARGS__))
    #define NO_THREAD_SAFETY_ANALYSIS   THREAD_SAFETY_ATTRIBUTE(no_thread_safety_analysis)

    #define PACKED_STRUCT ATTRIBUTE(packed)
    // clang-format on
#endif

#if !defined(NDEBUG)
    #define DEBUG_LOG(msg) std::cerr << msg << '\n'
#else
    #define DEBUG_LOG(msg) ((void) 0)
#endif

namespace DON {

namespace fs = std::filesystem;

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

using byte = std::byte;

#if defined(__SIZEOF_INT128__)
__extension__ using u128 = unsigned __int128;
__extension__ using i128 = signed __int128;
#endif

using uptr = std::uintptr_t;
using iptr = std::intptr_t;

// Size of cache line (in bytes)
inline constexpr usize CACHE_LINE_SIZE = 64;

using Strings     = std::vector<std::string>;
using StringViews = std::vector<std::string_view>;

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

template<typename T>
constexpr bool is_power_of_2(const T x) noexcept {
    return x != 0 && (x & (x - 1)) == 0;
}

template<typename T, std::enable_if_t<std::is_integral_v<T>, bool> = true>
constexpr std::make_unsigned_t<T> constexpr_abs(const T x) noexcept {
    using U = std::make_unsigned_t<T>;
    return x < 0 ? U{} - U(x) : U(x);
}
constexpr float       constexpr_abs(const float f) noexcept { return f < 0.0f ? -f : f; }
constexpr double      constexpr_abs(const double d) noexcept { return d < 0.0 ? -d : d; }
constexpr long double constexpr_abs(const long double ld) noexcept { return ld < 0.0L ? -ld : ld; }

constexpr int constexpr_ceil(const double d) noexcept { return static_cast<int>(d + 0.4999); }
constexpr int constexpr_floor(const double d) noexcept { return static_cast<int>(d - 0.4999); }
constexpr int constexpr_round(const double d) noexcept {
    return d < 0.0 ? constexpr_floor(d) : constexpr_ceil(d);
}

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

constexpr u64 bit(const u8 b) noexcept { return (u64{1} << b); }

// Computes ln(1 + d) for f in (-1, sqrt(2)-1] via the identity
// ln(1 + d) = 2 * atanh(s),   s = d / (d + 2.0)
// ln(1 + d) = 2 * (s + s^3/3 + s^5/5 + ...), s = d / (d + 2).
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

    // f = x - 1, approximately in (-0.293, 0.414), with |f| <= ~0.414.
    return constexpr_log1p_log(x - 1.0) + exponent * LN2;
}

template<typename T1, typename T2>
constexpr std::common_type_t<T1, T2> ceil_div(const T1 n, const T2 d) noexcept {
    using R = std::common_type_t<T1, T2>;
    //return (R(n) + R(d) - 1) / R(d);  // potential overflow issue
    return R(n) / R(d) + (R(n) % R(d) != 0);
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
#if defined(SIZE_MAX) && SIZE_MAX > 0xFFFFFFFF
    x |= x >> 32;  // for 64-bit usize
#endif
    return x + 1;
}

constexpr float max_load_factor(const float maxLoadFactor = 0.75f) noexcept {
    return std::clamp(constexpr_abs(maxLoadFactor), 0.1f, 1.0f);
}
constexpr usize reserve_count(const usize reserveCount = 1024) noexcept {
    return std::max(reserveCount, usize{8});
}

namespace _internal {

// Recursive template to define multi-dimensional array
template<typename T, usize Size, usize... Sizes>
struct ArrayDef final {
    using type = std::array<typename ArrayDef<T, Sizes...>::type, Size>;
};

// Base case: single-dimensional array
template<typename T, usize Size>
struct ArrayDef<T, Size> final {
    using type = std::array<T, Size>;
};

}  // namespace _internal

template<typename T, usize Size, usize... Sizes>
using Array = typename _internal::ArrayDef<T, Size, Sizes...>::type;

int hello_loop();

}  // namespace DON

#endif  // UTILITY_TYPES_H_INCLUDED
