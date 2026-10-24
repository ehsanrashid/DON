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
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#if !defined(_WIN32)     /* Non-Windows */
    #include <limits.h>  // PATH_MAX, NAME_MAX
#endif

#if !defined(PATH_MAX)
    #define PATH_MAX (2 * KB)  // 2K bytes, safe for almost all paths
#endif
#if !defined(NAME_MAX)
    #define NAME_MAX (255)
#endif

#define STRING_LITERAL(x) #x
#define STRINGIFY(x) STRING_LITERAL(x)

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

using SteadyClock = std::chrono::steady_clock;
using SystemClock = std::chrono::system_clock;
using Us          = std::chrono::microseconds;
using Ms          = std::chrono::milliseconds;

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

// Size of cache line (in bytes)
inline constexpr usize CACHE_LINE_SIZE = 64;

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

template<typename To, typename From>
constexpr bool is_strictly_assignable_v =
  std::is_assignable_v<To&, From> && (std::is_same_v<To, From> || !std::is_convertible_v<From, To>);

// True if and only if the binary is compiled on a little-endian machine
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
inline constexpr bool IS_LITTLE_ENDIAN = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;
#elif defined(_WIN32)
inline constexpr bool IS_LITTLE_ENDIAN = true;
#else
// Fallback runtime check
inline const bool IS_LITTLE_ENDIAN = []() noexcept -> bool {
    constexpr u16 LE = 1;
    return *reinterpret_cast<const u8*>(&LE) == 1;
}();
#endif

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
[[nodiscard]] constexpr int sign(const T x) noexcept {
    // NaN -> 0; unsigned types never return -1
    return (T(0) < x) - (x < T(0));  // Returns 1 for positive, -1 for negative, and 0 for zero
}

// Return the square of a number, using a wider type to avoid overflow
template<typename T>
[[nodiscard]] constexpr auto sqr(const T x) noexcept {
    static_assert(std::is_arithmetic_v<T>, "Argument must be arithmetic");
    using Wider = std::conditional_t<std::is_integral_v<T>, long long, T>;
    return Wider(x) * Wider(x);
}

// Return the square of a number multiplied by its sign, using a wider type to avoid overflow
template<typename T>
[[nodiscard]] constexpr auto sign_sqr(const T x) noexcept {
    static_assert(std::is_arithmetic_v<T>, "Argument must be arithmetic");
    return sign(x) * sqr(x);
}

[[nodiscard]] constexpr u64 bit(const u8 b) noexcept { return (u64{1} << b); }

template<typename T1, typename T2>
[[nodiscard]] constexpr std::common_type_t<T1, T2> ceil_div(const T1 n, const T2 d) noexcept {
    using R = std::common_type_t<T1, T2>;
    //return (R(n) + R(d) - 1) / R(d);  // potential overflow issue
    return R(n) / R(d) + (R(n) % R(d) != 0);
}

// Return round(value * numerator / denominator), capped at limit.
// The scale numerator is expected to be a small constant in callers.
[[nodiscard]] constexpr usize scaled_ratio_clamped(const usize value,
                                                   const usize numerator,
                                                   const usize denominator,
                                                   const usize limit) noexcept {
    assert(denominator != 0);

    if (denominator == 0 || numerator == 0 || limit == 0 || value == 0)
        return 0;

    // 1. Calculate the whole quotient section safely
    const usize quotient = value / denominator;
    if (quotient > limit / numerator)
        return limit;

    const usize whole     = quotient * numerator;
    const usize remainder = value % denominator;

    // 2. Compute the fractional part. Callers use a small scale numerator
    // (currently 6 or 15), so repeated modular addition is a cheap exact
    // fallback when remainder * numerator would overflow.
    usize fraction          = 0;
    usize fractionRemainder = 0;

    if (remainder == 0 || numerator <= std::numeric_limits<usize>::max() / remainder)
    {
        // Fast path when the product fits in usize.
        const usize product = remainder * numerator;
        fraction            = product / denominator;
        fractionRemainder   = product % denominator;
    }
    else
    {
        // Add remainder numerator times while keeping the intermediate
        // remainder below denominator. This computes the exact quotient and
        // remainder of (remainder * numerator) / denominator without forming
        // the potentially overflowing product.
        for (usize i = 0; i < numerator; ++i)
        {
            if (fractionRemainder >= denominator - remainder)
            {
                fractionRemainder -= denominator - remainder;
                ++fraction;
            }
            else
            {
                fractionRemainder += remainder;
            }
        }
    }

    // 3. Apply exact halves-upward rounding constraints
    const usize threshold = denominator - (denominator >> 1);
    if (fractionRemainder >= threshold)
        ++fraction;

    // 4. Verify structural safety margins
    if (fraction >= limit || whole > limit - fraction)
        return limit;

    return whole + fraction;
}

// Round n up to be a multiple of base
template<typename T>
[[nodiscard]] constexpr T ceil_to_multiple(const T n, const T base) noexcept {
    return ceil_div(n, base) * base;
}

// Round up to the next power of 2
[[nodiscard]] constexpr usize round_up_to_pow2(usize x) noexcept {
    if (x == 0)
        return 1;

    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
#if defined(SIZE_MAX) && SIZE_MAX == UINT64_MAX
    x |= x >> 32;
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
[[nodiscard]] T* align_ptr_up(T* ptr) noexcept {
    static_assert(is_power_of_2(Alignment), "Alignment must be non-zero power of 2");
    static_assert(Alignment >= alignof(T), "Alignment must be >= alignof(T)");

    const auto uPtr = round_up_to_multiple(reinterpret_cast<uptr>(ptr), uptr(Alignment));
    return reinterpret_cast<T*>(uPtr);
}

template<typename T1, typename T2>
[[nodiscard]] constexpr T2 interpolate(T1 x, T1 x0, T1 x1, T2 y0, T2 y1) noexcept {
    assert(x0 != x1);
    return T2(y0 + (y1 - y0) * (x - x0) / (x1 - x0));
}

[[nodiscard]] constexpr u64 mul_hi64(const u64 u1, const u64 u2) noexcept {
#if defined(__SIZEOF_INT128__)
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

}  // namespace DON

#endif  // UTILITY_TYPES_H_INCLUDED
