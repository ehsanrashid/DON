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
#include <exception>  // IWYU pragma: keep
// IWYU pragma: no_include <__exception/terminate.h>
#include <fstream>
#include <functional>
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
    #if !defined(PATH_MAX)
        #define PATH_MAX (2 * 1024)  // 2K bytes, safe for almost all paths
    #endif
    #if !defined(NAME_MAX)
        #define NAME_MAX 255
    #endif
#else
    #include <limits.h>  // IWYU pragma: keep
#endif

#undef HAS_X86_PREFETCH
#if defined(USE_PREFETCH) \
  && (defined(_M_X64) || defined(__x86_64__) || defined(__i386__) || defined(_M_IX86))
    #define HAS_X86_PREFETCH
    #include <xmmintrin.h>  // SSE intrinsics header for _mm_prefetch()
#endif

#include "memory.h"

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

using Strings     = std::vector<std::string>;
using StringViews = std::vector<std::string_view>;

inline constexpr std::size_t BYTE_BITS = 8;

inline constexpr std::size_t HEX64_SIZE = 16;
inline constexpr std::size_t HEX32_SIZE = 8;

inline constexpr std::size_t ONE_KB = 1024;
inline constexpr std::size_t ONE_MB = ONE_KB * ONE_KB;

inline constexpr std::size_t UNROLL_8 = 8;
inline constexpr std::size_t UNROLL_4 = 4;

inline constexpr std::int64_t INT_LIMIT = (1LL << 31) - 1;

// Constants for Murmur Hashing
inline constexpr std::uint64_t MURMUR_M = 0xC6A4A7935BD1E995ULL;
inline constexpr std::uint8_t  MURMUR_R = 47;

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
    constexpr std::uint16_t LE = 1;
    return *reinterpret_cast<const std::uint8_t*>(&LE) == 1;
}();
#endif

constexpr std::uint64_t bit(std::uint8_t b) noexcept { return (1ULL << b); }

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

constexpr std::size_t ceil_div(std::size_t n, std::size_t d) noexcept { return (n + d - 1) / d; }

constexpr std::size_t round_up_to_pow2(std::size_t x) noexcept {
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

template<typename T>
constexpr T constexpr_abs(T x) noexcept {
    static_assert(std::is_integral_v<T>);

    return x < 0 ? (x == std::numeric_limits<T>::min() ? x : -x) : x;
}

constexpr float constexpr_abs(float f) noexcept { return f < 0 ? -f : f; }

constexpr int constexpr_round(double d) noexcept {
    return d >= 0.0 ? int(d + 0.4999) : int(d - 0.4999);
}

// Minimax-style polynomial approximation for ln(1 + f), f in [0,1)
constexpr double constexpr_approx_1p_log(double f) noexcept {
    // clang-format off
    return f * ( 1.0
         + f * (-0.5
         + f * ( 0.33333333333333333
         + f * (-0.25
         + f * ( 0.2
         + f * (-0.16666666666666666))))));
    // clang-format on
}

// constexpr natural logarithm using range reduction + polynomial
constexpr double constexpr_log(double x) noexcept {
    constexpr double LN2 = 0.693147180559945309417232121458176568;

    if (x <= 0.0)
        return -1e300;  // Undefined, but safe for compile-time tables

    int exponent = 0;

    // Range reduction: x = mantissa * 2^exponent : normalize x into [1, 2)
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

    // mantissa in [1,2) -> f in [0,1)
    // ln(x) = ln(m) + exponent * ln(2)
    return constexpr_approx_1p_log(x - 1.0) + exponent * LN2;
}

constexpr float max_load_factor(float maxLoadFactor = 0.75f) noexcept {
    return std::clamp(constexpr_abs(maxLoadFactor), 0.1f, 1.0f);
}
constexpr std::size_t reserve_count(std::size_t reserveCount = 1024) noexcept {
    return std::max(reserveCount, std::size_t(4));
}

std::string engine_info(bool uci = false) noexcept;

std::string version_info() noexcept;

std::string compiler_info() noexcept;

constexpr std::uint64_t mul_hi64(std::uint64_t u1, std::uint64_t u2) noexcept {
#if defined(__GNUC__) && defined(IS_64BIT)
    __extension__ using uint128 = unsigned __int128;
    return (uint128(u1) * uint128(u2)) >> 64;
#else
    std::uint64_t u1L = std::uint32_t(u1), u1H = u1 >> 32;
    std::uint64_t u2L = std::uint32_t(u2), u2H = u2 >> 32;
    std::uint64_t mid = u1H * u2L + ((u1L * u2L) >> 32);
    return u1H * u2H + ((u1L * u2H + std::uint32_t(mid)) >> 32) + (mid >> 32);
#endif
}

static_assert(mul_hi64(0xDEADBEEFDEADBEEFULL, 0xCAFEBABECAFEBABEULL) == 0xB092AB7CE9F4B259ULL,
              "mul_hi64(): Failed");

// PrefetchAccess for explicit call-site control
enum class PrefetchAccess : std::uint8_t {
    READ,
    WRITE
};

// PrefetchLoc controls locality / cache level, not whether a prefetch is issued.
// In particular, PrefetchLoc::NONE maps to a non-temporal / lowest-locality prefetch
// (Intel: _MM_HINT_NTA, GCC/Clang: locality = 0) and therefore still performs a prefetch.
enum class PrefetchLoc : std::uint8_t {
    NONE,      // Non-temporal / no cache locality (still issues a prefetch)
    LOW,       // Low locality (e.g. T2 / L2)
    MODERATE,  // Moderate locality (e.g. T1 / L1)
    HIGH       // High locality (e.g. T0 / closest cache)
};

#if defined(USE_PREFETCH)
// Preloads the given address into cache.
// Non-blocking operation that doesn't stall the CPU waiting for data to be loaded from memory.
// NOTE:
// On x86, _mm_prefetch does NOT truly distinguish READ vs WRITE.
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
static_assert(sizeof(TimePoint) == sizeof(std::int64_t), "TimePoint size must be 8 bytes");

inline TimePoint now() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string format_time(const std::chrono::system_clock::time_point& timePoint) noexcept;

struct IndexCount final {
   public:
    std::size_t begIdx;
    std::size_t count;
};

constexpr IndexCount
thread_index_count(std::size_t threadId, std::size_t threadCount, std::size_t size) noexcept {
    assert(threadCount != 0 && threadId < threadCount);

    std::size_t stride = size / threadCount;
    std::size_t remain = size % threadCount;  // remainder to distribute

    // Distribute remainder among the first 'remain' threads
    std::size_t begIdx = threadId * stride + std::min(threadId, remain);
    std::size_t count  = stride + std::size_t(threadId < remain);

    assert(begIdx + count <= size);
    return {begIdx, count};
}

struct IndexRange final {
   public:
    std::size_t begIdx;
    std::size_t endIdx;
};

constexpr IndexRange
thread_index_range(std::size_t threadId, std::size_t threadCount, std::size_t size) noexcept {
    assert(threadCount != 0 && threadId < threadCount);

    std::size_t stride = size / threadCount;
    std::size_t remain = size % threadCount;  // remainder to distribute

    // Distribute remainder among the first 'remain' threads
    std::size_t begIdx = threadId * stride + std::min(threadId, remain);
    std::size_t endIdx = begIdx + stride + std::size_t(threadId < remain);

    assert(begIdx <= endIdx && endIdx <= size);
    return {begIdx, endIdx};
}

struct CallOnce final {
   public:
    CallOnce()                           = default;
    CallOnce(const CallOnce&)            = delete;
    CallOnce(CallOnce&&)                 = delete;
    CallOnce& operator=(const CallOnce&) = delete;
    CallOnce& operator=(CallOnce&&)      = delete;

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
    LazyValue()                            = default;
    LazyValue(const LazyValue&)            = delete;
    LazyValue(LazyValue&&)                 = delete;
    LazyValue& operator=(const LazyValue&) = delete;
    LazyValue& operator=(LazyValue&&)      = delete;

    ~LazyValue() noexcept {
        if (is_initialized())
            get_ptr()->~Value();
    }

    template<typename... Args>
    Value& init(Args&&... args) noexcept(std::is_nothrow_constructible_v<Value, Args...>) {
        // Fast path: already initialized
        if (is_initialized())
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
        assert(is_initialized() && "LazyValue accessed before initialization");
        return *get_ptr();
    }

    const Value& get() const noexcept {
        assert(is_initialized() && "LazyValue accessed before initialization");
        return *get_ptr();
    }

    [[nodiscard]] bool is_initialized() const noexcept { return callOnce.initialized(); }

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
    static void ensure_initialized(std::size_t reserveCount  = 16,
                                   float       maxLoadFactor = 0.85f) noexcept {
        callOnce([reserveCount, maxLoadFactor]() noexcept {
            osMutexes.max_load_factor(max_load_factor(maxLoadFactor));
            osMutexes.reserve(reserve_count(reserveCount));
        });
    }

    // Return a mutex associated with the given ostream pointer.
    // If osPtr is nullptr, returns a null-mutex to safely ignore locking.
    // This ensures no accidental insertion of null keys into the map.
    static std::mutex& get(std::ostream* osPtr) noexcept {
        ensure_initialized();

        // Fallback for null pointers
        if (osPtr == nullptr)
            return nullMutex;

        // Lock the registry while accessing the map
        std::lock_guard writeLock(mutex);

        // Return mutex, create if missing
        return osMutexes[osPtr];
    }

   private:
    OstreamMutexRegistry() noexcept                                       = delete;
    ~OstreamMutexRegistry() noexcept                                      = delete;
    OstreamMutexRegistry(const OstreamMutexRegistry&) noexcept            = delete;
    OstreamMutexRegistry(OstreamMutexRegistry&&) noexcept                 = delete;
    OstreamMutexRegistry& operator=(const OstreamMutexRegistry&) noexcept = delete;
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
        osPtr(&os),
        lock(OstreamMutexRegistry::get(osPtr)) {}
    SyncOstream(const SyncOstream&) noexcept = delete;
    // Move-constructible so factories can return by value
    SyncOstream(SyncOstream&& syncOs) noexcept :
        osPtr(syncOs.osPtr),
        lock(std::move(syncOs.lock)) {}

    SyncOstream& operator=(const SyncOstream&) noexcept = delete;
    // Prefer deleting move-assignment to avoid unlock window
    SyncOstream& operator=(SyncOstream&&) noexcept = delete;

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

    using IosManipulator = std::ios& (*) (std::ios&);

    SyncOstream& operator<<(IosManipulator manip) & {
        assert(osPtr != nullptr && "Use of moved-from SyncOstream");

        manip(*osPtr);
        return *this;
    }
    SyncOstream&& operator<<(IosManipulator manip) && {
        assert(osPtr != nullptr && "Use of moved-from SyncOstream");

        manip(*osPtr);
        return std::move(*this);
    }

    using OstreamManipulator = std::ostream& (*) (std::ostream&);

    SyncOstream& operator<<(OstreamManipulator manip) & {
        assert(osPtr != nullptr && "Use of moved-from SyncOstream");

        manip(*osPtr);
        return *this;
    }
    SyncOstream&& operator<<(OstreamManipulator manip) && {
        assert(osPtr != nullptr && "Use of moved-from SyncOstream");

        manip(*osPtr);
        return std::move(*this);
    }

   private:
    std::ostream* const          osPtr;
    std::unique_lock<std::mutex> lock;
};

[[nodiscard]] inline SyncOstream sync_os(std::ostream& os = std::cout) noexcept {
    return SyncOstream(os);
}

// --- TableView with pointer and size ---
template<typename T>
class TableView final {
   public:
    constexpr TableView(T* data, std::size_t size) noexcept :
        _data(data),
        _size(size) {}

    constexpr T*       data() noexcept { return _data; }
    constexpr const T* data() const noexcept { return _data; }

    constexpr std::size_t size() const noexcept { return _size; }

    constexpr T* begin() noexcept { return data(); }
    constexpr T* end() noexcept { return begin() + size(); }
    constexpr T* begin() const noexcept { return data(); }
    constexpr T* end() const noexcept { return begin() + size(); }

    constexpr T& operator[](std::size_t idx) noexcept {
        assert(idx < size());
        return data()[idx];
    }
    constexpr T& operator[](std::size_t idx) const noexcept {
        assert(idx < size());
        return data()[idx];
    }

   private:
    T*          _data = nullptr;
    std::size_t _size = 0;
};

// --- OffsetView with offset + size ---
template<typename T>
struct OffsetView final {
    using off_type  = std::uint8_t;
    using size_type = std::uint8_t;

    constexpr OffsetView() noexcept = default;
    constexpr OffsetView(off_type offset, size_type size) noexcept :
        _offset(offset),
        _size(size) {}

    void set(off_type offset, size_type size) noexcept {
        _offset = offset;
        _size   = size;
    }

    constexpr off_type  offset() const noexcept { return _offset; }
    constexpr size_type size() const noexcept { return _size; }

    // --- Access data from a base pointer ---
    constexpr T*       data(T* base) noexcept { return base + offset(); }
    constexpr const T* data(const T* base) const noexcept { return base + offset(); }

    // --- Push/pop using external count ---
    void push_back(const T& value, T* base, size_type count) noexcept {
        assert(count < size());

        data(base)[count] = value;
    }
    void push_back(T&& value, T* base, size_type count) noexcept {
        assert(count < size());

        data(base)[count] = std::move(value);
    }

    T& back(T* base, size_type count) noexcept {
        assert(count != 0);

        return data(base)[count - 1];
    }
    const T& back(const T* base, size_type count) const noexcept {
        assert(count != 0);

        return data(base)[count - 1];
    }

    // --- Element access ---
    T&       at(size_type idx, T* base) noexcept { return data(base)[idx]; }
    const T& at(size_type idx, const T* base) const noexcept { return data(base)[idx]; }

    // --- STL-style iterable proxy ---
    struct Iterable final {
       public:
        T*       begin() { return base; }
        T*       end() { return begin() + count; }
        const T* begin() const { return base; }
        const T* end() const { return begin() + count; }

        T*        base;
        size_type count;
    };

    // --- Return iterable for range-based for ---
    Iterable iterate(T* base, size_type count) noexcept {
        assert(count <= size());

        return {data(base), count};
    }
    const Iterable iterate(const T* base, size_type count) const noexcept {
        assert(count <= size());

        return {const_cast<T*>(data(base)), count};
    }

   private:
    off_type  _offset = 0;
    size_type _size   = 0;
};

template<typename T, std::size_t Size, std::size_t... Sizes>
class MultiArray;

namespace internal {

template<typename T, std::size_t Size, std::size_t... Sizes>
struct StdArrayDef final {
    static_assert(Size >= 0, "dimension must be >= 0");
    using type = std::array<typename StdArrayDef<T, Sizes...>::type, Size>;
};

template<typename T, std::size_t Size>
struct StdArrayDef<T, Size> final {
    static_assert(Size >= 0, "dimension must be >= 0");
    using type = std::array<T, Size>;
};

// Recursive template to define multi-dimensional array
template<typename T, std::size_t Size, std::size_t... Sizes>
struct MultiArrayDef final {
    static_assert(Size >= 0, "dimension must be >= 0");
    using Type = MultiArray<T, Sizes...>;
};
// Base case: single-dimensional array
template<typename T, std::size_t Size>
struct MultiArrayDef<T, Size> final {
    static_assert(Size >= 0, "dimension must be >= 0");
    using Type = T;
};

}  // namespace internal

template<typename T, std::size_t Size, std::size_t... Sizes>
using StdArray = typename internal::StdArrayDef<T, Size, Sizes...>::type;

// MultiArray is a generic N-dimensional array.
// The template parameter T is the base type of the MultiArray
// The template parameters (Size and Sizes) is the dimensions of the MultiArray.
template<typename T, std::size_t Size, std::size_t... Sizes>
class MultiArray {
    using ElementType = typename internal::MultiArrayDef<T, Size, Sizes...>::Type;
    using ArrayType   = StdArray<ElementType, Size>;

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

    constexpr auto begin() const noexcept { return _data.begin(); }
    constexpr auto end() const noexcept { return _data.end(); }
    constexpr auto begin() noexcept { return _data.begin(); }
    constexpr auto end() noexcept { return _data.end(); }

    constexpr auto cbegin() const noexcept { return _data.cbegin(); }
    constexpr auto cend() const noexcept { return _data.cend(); }

    constexpr auto rbegin() const noexcept { return _data.rbegin(); }
    constexpr auto rend() const noexcept { return _data.rend(); }
    constexpr auto rbegin() noexcept { return _data.rbegin(); }
    constexpr auto rend() noexcept { return _data.rend(); }

    constexpr auto crbegin() const noexcept { return _data.crbegin(); }
    constexpr auto crend() const noexcept { return _data.crend(); }

    constexpr auto&       front() noexcept { return _data.front(); }
    constexpr const auto& front() const noexcept { return _data.front(); }
    constexpr auto&       back() noexcept { return _data.back(); }
    constexpr const auto& back() const noexcept { return _data.back(); }

    auto*       data() noexcept { return _data.data(); }
    const auto* data() const noexcept { return _data.data(); }

    constexpr auto max_size() const noexcept { return _data.max_size(); }

    constexpr auto size() const noexcept { return _data.size(); }
    constexpr auto empty() const noexcept { return _data.empty(); }

    constexpr const auto& at(size_type idx) const noexcept { return _data.at(idx); }
    constexpr auto&       at(size_type idx) noexcept { return _data.at(idx); }

    constexpr auto& operator[](size_type idx) const noexcept { return _data[idx]; }
    constexpr auto& operator[](size_type idx) noexcept { return _data[idx]; }

    // Recursively fill all dimensions by calling the sub fill method
    template<typename U>
    void fill(const U& v) noexcept {
        static_assert(is_strictly_assignable_v<T, U>, "Cannot assign fill value to element type");

        for (auto& element : _data)
        {
            if constexpr (sizeof...(Sizes) == 0)
                element = v;
            else
                element.fill(v);
        }
    }

    template<typename U>
    void fill_n(std::size_t begIdx, std::size_t count, const U& v) noexcept {
        static_assert(is_strictly_assignable_v<T, U>, "Cannot assign fill value to element type");

        std::size_t endIdx = std::min(begIdx + count, size());
        assert(begIdx <= endIdx && endIdx <= size());

        for (std::size_t idx = begIdx; idx < endIdx; ++idx)
        {
            if constexpr (sizeof...(Sizes) == 0)
                _data[idx] = v;
            else
                _data[idx].fill(v);
        }
    }

    // void print() const noexcept {
    //     std::cout << Size << ':' << sizeof...(Sizes) << std::endl;
    //
    //     for (auto& element : _data)
    //     {
    //         if constexpr (sizeof...(Sizes) == 0)
    //             std::cout << element << ' ';
    //         else
    //             element.print();
    //     }
    //
    //     std::cout << std::endl;
    // }

    constexpr void swap(MultiArray<T, Size, Sizes...>& multiArr) noexcept {
        _data.swap(multiArr._data);
    }

    template<bool NoExtraDimension = sizeof...(Sizes) == 0,
             typename              = typename std::enable_if_t<NoExtraDimension, bool>>
    constexpr operator StdArray<T, Size>&() noexcept {
        return _data;
    }
    template<bool NoExtraDimension = sizeof...(Sizes) == 0,
             typename              = typename std::enable_if_t<NoExtraDimension, bool>>
    constexpr operator const StdArray<T, Size>&() const noexcept {
        return _data;
    }

    constexpr MultiArray& operator=(const StdArray<T, Size, Sizes...>& stdArr) noexcept {
        for (std::size_t i = 0; i < Size; ++i)
            _data[i] = stdArr[i];
        return *this;
    }

   private:
    ArrayType _data;
};

template<typename T>
class DynamicArray final {
   public:
    explicit DynamicArray(std::size_t size) noexcept :
        _size(size) {
        assert(size != 0);

        _data = make_unique_aligned_large_page<T[]>(size);
    }

    std::size_t size() const noexcept { return _size; }

    T*       data() noexcept { return _data.get(); }
    const T* data() const noexcept { return _data.get(); }

    T& operator[](std::size_t idx) noexcept {
        assert(idx < size());
        return data()[idx];
    }
    const T& operator[](std::size_t idx) const noexcept {
        assert(idx < size());
        return data()[idx];
    }

    template<typename U>
    void fill(std::size_t begIdx, std::size_t endIdx, const U& v) noexcept {
        assert(begIdx <= endIdx && endIdx <= size());

        for (std::size_t idx = begIdx; idx < endIdx; ++idx)
            data()[idx].fill(v);
    }

   private:
    LargePagePtr<T[]> _data;
    std::size_t       _size;
};

template<typename T, std::size_t Capacity, typename SizeType = std::size_t>
class FixedVector final {
    static_assert(Capacity > 0, "Capacity must be > 0");

   public:
    [[nodiscard]] constexpr SizeType capacity() const noexcept { return Capacity; }

    [[nodiscard]] constexpr SizeType size() const noexcept { return _size; }
    [[nodiscard]] constexpr bool     empty() const noexcept { return size() == 0; }
    [[nodiscard]] constexpr bool     full() const noexcept { return size() == capacity(); }

    T*       data() noexcept { return _data.data(); }
    const T* data() const noexcept { return _data.data(); }

    T*       begin() noexcept { return data(); }
    T*       end() noexcept { return begin() + size(); }
    const T* begin() const noexcept { return data(); }
    const T* end() const noexcept { return begin() + size(); }
    const T* cbegin() const noexcept { return data(); }
    const T* cend() const noexcept { return cbegin() + size(); }

    bool push_back(const T& value) noexcept {
        assert(size() < capacity());

        data()[_size++] = value;  // copy-assign into pre-initialized slot
        return true;
    }
    bool push_back(T&& value) noexcept {
        assert(size() < capacity());

        data()[_size++] = std::move(value);
        return true;
    }
    template<typename... Args>
    bool emplace_back(Args&&... args) noexcept {
        assert(size() < capacity());

        data()[_size++] = T(std::forward<Args>(args)...);
        return true;
    }

    void pop_back() noexcept {
        assert(size() != 0);

        --_size;
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
        _size = std::min(newSize, capacity());
    }

    T* make_space(SizeType space) noexcept {
        SizeType oldSize = size();

        resize(oldSize + space);

        return data() + oldSize;
    }

    void clear() noexcept { _size = 0; }

   private:
    StdArray<T, Capacity> _data;
    SizeType              _size = 0;
};

template<std::size_t Capacity>
class FixedString final {
    static_assert(Capacity > 0, "Capacity must be > 0");

   public:
    FixedString() noexcept { clear(); }

    FixedString(std::string_view str) { assign(str); }

    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return Capacity; }

    [[nodiscard]] std::size_t size() const noexcept { return _size; }
    [[nodiscard]] bool        empty() const noexcept { return size() == 0; }
    [[nodiscard]] bool        full() const noexcept { return size() == capacity(); }

    constexpr char*       data() noexcept { return _data.data(); }
    constexpr const char* data() const noexcept { return _data.data(); }

    constexpr const char* c_str() const noexcept { return data(); }

    constexpr char*       begin() noexcept { return data(); }
    constexpr char*       end() noexcept { return begin() + size(); }
    constexpr const char* begin() const noexcept { return data(); }
    constexpr const char* end() const noexcept { return begin() + size(); }
    constexpr const char* cbegin() const noexcept { return data(); }
    constexpr const char* cend() const noexcept { return cbegin() + size(); }

    constexpr char& operator[](std::size_t idx) noexcept {
        assert(idx < size());

        return data()[idx];
    }
    constexpr const char& operator[](std::size_t idx) const noexcept {
        assert(idx < size());

        return data()[idx];
    }

    void null_terminate() noexcept { data()[size()] = '\0'; }

    FixedString& operator=(std::string_view str) {
        assign(str);
        return *this;
    }
    // Optional: assignment from const char*
    FixedString& operator=(const char* str) {
        assign(str);
        return *this;
    }

    FixedString& operator+=(std::string_view str) {

        if (size() + str.size() > capacity())
            std::terminate();

        std::memcpy(data() + size(), str.data(), str.size());

        _size += str.size();
        null_terminate();

        return *this;
    }

    FixedString& operator+=(const FixedString& fixedStr) {
        *this += fixedStr.data();

        return *this;
    }

    operator std::string() const noexcept { return std::string{data(), size()}; }

    operator std::string_view() const noexcept { return std::string_view{data(), size()}; }

    template<typename T>
    bool operator==(const T& t) const noexcept {
        return (std::string_view) (*this) == t;
    }
    template<typename T>
    bool operator!=(const T& t) const noexcept {
        return !(*this == t);
    }

    void clear() noexcept {
        _size = 0;
        null_terminate();
    }

   private:
    void assign(std::string_view str) {

        if (str.size() > capacity())
            std::terminate();

        std::memcpy(data(), str.data(), str.size());

        _size = str.size();
        null_terminate();
    }

    StdArray<char, Capacity + 1> _data;  // +1 for null terminator
    std::size_t                  _size;
};

// ConcurrentCache: groups (mutex + storage + pre-reserve)
template<typename Key, typename Value>
class ConcurrentCache final {
   public:
    ConcurrentCache(std::size_t reserveCount = 1024, float maxLoadFactor = 0.75f) noexcept {
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
    static constexpr std::size_t THRESHOLD_SIZE = 128;

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

    Value& get_value(StorageValue& entry) noexcept {
        if constexpr (sizeof(Value) <= THRESHOLD_SIZE)
            return entry;
        else
            return *entry;
    }

    std::shared_mutex                     sharedMutex;
    std::unordered_map<Key, StorageValue> storage;
};

// RAII guard for resetting atomic bool flags
struct FlagGuard final {
   public:
    explicit FlagGuard(std::atomic<bool>& flagRef) noexcept :
        flag(flagRef) {}

    ~FlagGuard() noexcept { reset(); }

    // Manually reset the flag if needed before destruction
    void reset() noexcept { flag.store(false, std::memory_order_release); }

   private:
    // Non-copyable, non-movable to ensure unique ownership
    FlagGuard(const FlagGuard&)            = delete;
    FlagGuard(FlagGuard&&)                 = delete;
    FlagGuard& operator=(const FlagGuard&) = delete;
    FlagGuard& operator=(FlagGuard&&)      = delete;

    std::atomic<bool>& flag;
};

// RAII guard for resetting atomic int flags
template<typename T>
struct FlagsGuard final {
   public:
    explicit FlagsGuard(std::atomic<T>& flagsRef) noexcept :
        flags(flagsRef) {}

    ~FlagsGuard() noexcept { reset(); }

    // Manually reset the flag if needed before destruction
    void reset() noexcept { flags.store(0, std::memory_order_release); }

   private:
    // Non-copyable, non-movable to ensure unique ownership
    FlagsGuard(const FlagsGuard&)            = delete;
    FlagsGuard(FlagsGuard&&)                 = delete;
    FlagsGuard& operator=(const FlagsGuard&) = delete;
    FlagsGuard& operator=(FlagsGuard&&)      = delete;

    std::atomic<T>& flags;
};

// Hash function based on public domain MurmurHash64A by Austin Appleby.
// Fast, non-cryptographic 64-bit hash suitable for general-purpose hashing.
inline std::uint64_t
hash_bytes(const char* RESTRICT data, std::size_t size, std::uint64_t seed = 0) noexcept {
    // Initialize hash with seed and length (MurmurHash64A convention)
    std::uint64_t h = seed ^ (size * MURMUR_M);

    const std::uint8_t* const RESTRICT dataBeg = reinterpret_cast<const std::uint8_t*>(data);

    const std::uint8_t* const RESTRICT dataEnd = dataBeg + size;
    // End of the full 8-byte blocks (size rounded down to a multiple of 8)
    const std::uint8_t* const RESTRICT chunkEnd = dataEnd - (size & (UNROLL_8 - 1));

    const std::uint8_t* RESTRICT p = dataBeg;
    // Process the data in 8-byte (64-bit) chunks
    for (; p < chunkEnd; p += UNROLL_8)
    {
        std::uint64_t k;
        std::memcpy(&k, p, sizeof(k));  // Safe unaligned load

        // Mix 64-bit block (MurmurHash64A core mixing step)
        k *= MURMUR_M;
        k ^= k >> MURMUR_R;
        k *= MURMUR_M;
        // Incorporate block into the hash
        h ^= k;
        h *= MURMUR_M;
    }
    // Handle remaining tail bytes (< 8) at the end
    {
        std::uint64_t k = 0;

        std::uint8_t shift = 0;
        // Read remaining bytes in little-endian order
        while (p < dataEnd)
        {
            k |= std::uint64_t(*p) << shift;

            shift += BYTE_BITS;
            ++p;
        }

        if (shift != 0)  // Only process if there were tail bytes
        {
            h ^= k;
            h *= MURMUR_M;
        }
    }

    // Final avalanche mix to ensure strong bit diffusion
    h ^= h >> MURMUR_R;
    h *= MURMUR_M;
    h ^= h >> MURMUR_R;

    return h;
}

inline std::uint64_t hash_string(std::string_view str) noexcept {
    return hash_bytes(str.data(), str.size());
}

template<typename T>
std::uint64_t hash_raw_data(const T& value) noexcept {
    // Must have no padding bytes because reinterpreting as char*
    static_assert(std::has_unique_object_representations<T>());

    return hash_bytes(reinterpret_cast<const char*>(&value), sizeof(value));
}

template<typename T>
void combine_hash(std::size_t& seed, const T& v) noexcept {
    std::size_t x;
    // For primitive types we avoid using the default hasher, which may be
    // nondeterministic across program invocations
    if constexpr (std::is_integral<T>())
        x = v;
    else
        x = std::hash<T>{}(v);

    seed ^= x + 0x9E3779B9U + (seed << 6) + (seed >> 2);
}

// C++ way to prepare a buffer for a memory stream
class MemoryStreamBuf final: public std::streambuf {
   public:
    MemoryStreamBuf(char* p, std::size_t n) noexcept {
        setg(p, p, p + n);
        setp(p, p + n);
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

    std::streambuf* pbuf() { return pBuf; }
    std::streambuf* mbuf() { return mBuf; }

   private:
    int_type
    mirror_put_with_prefix(int_type ch, std::string_view prefix, char_type& preCh) noexcept {
        if (mBuf == nullptr)
            return traits_type::not_eof(ch);

        if (preCh == '\n')
            mBuf->sputn(prefix.data(), std::streamsize(prefix.size()));

        return mBuf->sputc(preCh = traits_type::to_char_type(ch));
    }

    std::streambuf *pBuf, *mBuf;

    char_type preOutCh = '\n';
    char_type preInCh  = '\n';
};

class Logger final {
   public:
    // Start logging. Returns true on success.
    static bool start(std::string_view logFile) noexcept {
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
    Logger(std::istream& _is, std::ostream& _os) noexcept :
        is(_is),
        os(_os),
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

    bool is_open() const noexcept { return ofs.is_open(); }

    void write_timestamp(std::string_view suffix) noexcept {
        if (!ofs.is_open())
            return;

        ofs << '[' << format_time(std::chrono::system_clock::now()) << "] " << suffix << std::endl;
    }

    // Open log file; caller must hold mutex
    // If another file is already open, it will be closed first.
    bool open(std::string_view logFile) noexcept {
        if (logFile == filename && is_open())
            return true;  // Already open

        close();  // Close any previous log file

        if (logFile.empty())
            return true;

        filename = logFile;

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
void hit_on(bool cond, std::size_t slot = 0) noexcept;
void min_of(std::int64_t value, std::size_t slot = 0) noexcept;
void max_of(std::int64_t value, std::size_t slot = 0) noexcept;
void extreme_of(std::int64_t value, std::size_t slot = 0) noexcept;
void mean_of(std::int64_t value, std::size_t slot = 0) noexcept;
void stdev_of(std::int64_t value, std::size_t slot = 0) noexcept;
void correl_of(std::int64_t value1, std::int64_t value2, std::size_t slot = 0) noexcept;

void print() noexcept;
}  // namespace Debug
#endif

struct CommandLine final {
   public:
    CommandLine(int argc, const char* argv[]) noexcept;

    static std::string binary_directory(std::string_view path) noexcept;
    static std::string working_directory() noexcept;

    StringViews arguments;
};

[[nodiscard]] constexpr char digit_to_char(int digit) noexcept {
    assert(0 <= digit && digit <= 9 && "digit_to_char: non-digit integer");

    return 0 <= digit && digit <= 9 ? digit + '0' : '\0';
}

[[nodiscard]] constexpr int char_to_digit(char ch) noexcept {
    assert('0' <= ch && ch <= '9' && "char_to_digit: non-digit character");

    return '0' <= ch && ch <= '9' ? ch - '0' : -1;
}

inline std::string lower_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char ch) noexcept -> char { return std::tolower(ch); });
    return str;
}

inline std::string upper_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char ch) noexcept -> char { return std::toupper(ch); });
    return str;
}

inline std::string toggle_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char ch) noexcept -> char {
        return std::islower(ch) ? std::toupper(ch) : std::isupper(ch) ? std::tolower(ch) : ch;
    });
    return str;
}

inline std::string remove_whitespace(std::string str) noexcept {
    str.erase(std::remove_if(str.begin(), str.end(),
                             [](unsigned char ch) noexcept { return std::isspace(ch); }),
              str.end());
    return str;
}

[[nodiscard]] constexpr bool starts_with(std::string_view str, std::string_view prefix) noexcept {
    return str.size() >= prefix.size()  //
        && str.compare(0, prefix.size(), prefix) == 0;
}

[[nodiscard]] constexpr bool ends_with(std::string_view str, std::string_view suffix) noexcept {
    return str.size() >= suffix.size()  //
        && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

[[nodiscard]] constexpr bool is_whitespace(std::string_view str) noexcept {
    return str.find_first_not_of(WHITE_SPACE) == std::string_view::npos;
}

[[nodiscard]] constexpr std::string_view ltrim(std::string_view str) noexcept {
    // Find the first non-whitespace character
    std::size_t beg = str.find_first_not_of(WHITE_SPACE);

    if (beg == std::string_view::npos)
        return {};

    return str.substr(beg);
}

[[nodiscard]] constexpr std::string_view rtrim(std::string_view str) noexcept {
    // Find the last non-whitespace character
    std::size_t end = str.find_last_not_of(WHITE_SPACE);

    if (end == std::string_view::npos)
        return {};

    return str.substr(0, end + 1);
}

[[nodiscard]] constexpr std::string_view trim(std::string_view str) noexcept {
    std::size_t beg = str.find_first_not_of(WHITE_SPACE);

    if (beg == std::string_view::npos)
        return {};

    std::size_t end = str.find_last_not_of(WHITE_SPACE);

    return str.substr(beg, end - beg + 1);
}

[[nodiscard]] constexpr std::string_view bool_to_string(bool b) noexcept {
    return b ? "true" : "false";
}
// Efficient check: works for std::string or std::string_view
[[nodiscard]] constexpr bool valid_bool_string(std::string_view value) noexcept {
    return value == "true" || value == "false";
}

[[nodiscard]] constexpr bool string_to_bool(std::string_view str) { return (trim(str) == "true"); }

inline std::string clamp_string(std::string_view value, int minValue, int maxValue) noexcept {
    int intValue = 0;

    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), intValue);

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
split(std::string_view str, std::string_view delimiter, bool trimPart = false) noexcept {
    StringViews parts;

    if (str.empty() || delimiter.empty())
        return parts;  // Avoid infinite loop for empty delimiter

    std::string_view part;

    std::size_t beg = 0;

    while (true)
    {
        std::size_t end = str.find(delimiter, beg);

        if (end == std::string_view::npos)
            break;

        part = str.substr(beg, end - beg);

        if (trimPart)
            part = trim(part);

        if (!is_whitespace(part))
            parts.emplace_back(part);

        beg = end + delimiter.size();
    }

    // Last part
    part = str.substr(beg);

    if (trimPart)
        part = trim(part);

    if (!is_whitespace(part))
        parts.emplace_back(part);

    return parts;
}

inline std::string hash_to_string(std::uint64_t hash) noexcept {
    constexpr std::size_t BufferSize = HEX64_SIZE + 1;  // 16 hex + '\0'

    StdArray<char, BufferSize> buffer{};

    int         writtenSize = std::snprintf(buffer.data(), buffer.size(), "%016" PRIX64, hash);
    std::size_t copiedSize  = writtenSize >= 0  //
                              ? std::min<std::size_t>(writtenSize, buffer.size() - 1)
                              : 0;

    return std::string{buffer.data(), copiedSize};
}

inline std::string u32_to_string(std::uint32_t u32) noexcept {
    constexpr std::size_t BufferSize = 2 + HEX32_SIZE + 1;  // "0x" + 8 hex + '\0'

    StdArray<char, BufferSize> buffer{};

    int         writtenSize = std::snprintf(buffer.data(), buffer.size(), "0x%08" PRIX32, u32);
    std::size_t copiedSize  = writtenSize >= 0  //
                              ? std::min<std::size_t>(writtenSize, buffer.size() - 1)
                              : 0;

    return std::string{buffer.data(), copiedSize};
}
inline std::string u64_to_string(std::uint64_t u64) noexcept {
    constexpr std::size_t BufferSize = 2 + HEX64_SIZE + 1;  // "0x" + 16 hex + '\0'

    StdArray<char, BufferSize> buffer{};

    int         writtenSize = std::snprintf(buffer.data(), buffer.size(), "0x%016" PRIX64, u64);
    std::size_t copiedSize  = writtenSize >= 0  //
                              ? std::min<std::size_t>(writtenSize, buffer.size() - 1)
                              : 0;

    return std::string{buffer.data(), copiedSize};
}

std::size_t str_to_size_t(std::string_view str) noexcept;

// Reads the file as bytes.
// Returns std::nullopt if the file does not exist.
std::optional<std::string> read_file_to_string(std::string_view filePath) noexcept;

#if defined(_WIN32)
// Get the error message string, if any
inline std::string error_to_string(DWORD errorId) noexcept {
    if (errorId == 0)
        return {};

    LPSTR buffer = nullptr;
    // Ask Win32 to give us the string version of that message ID.
    // The parameters pass in, tell Win32 to create the buffer that holds the message
    // (because don't yet know how long the message string will be).
    std::size_t size = FormatMessage(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL, errorId, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&buffer),  // must pass pointer to buffer pointer
      0, NULL);

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
#endif

}  // namespace DON

template<std::size_t N>
struct std::hash<DON::FixedString<N>> {
    std::size_t operator()(const DON::FixedString<N>& fixedStr) const noexcept {
        return DON::hash_bytes(fixedStr.data(), fixedStr.size());
    }
};

#endif  // #ifndef MISC_H_INCLUDED
