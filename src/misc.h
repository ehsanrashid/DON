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
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#if defined(USE_PREFETCH) && defined(X86) && (defined(_MSC_VER) || defined(__INTEL_COMPILER))
    #define USE_X86_PREFETCH
#endif

#if defined(_WIN32)
    #include "platform_win.h"  // GetCommandLineW(), PreFetchCacheLine()
#endif

#if defined(USE_X86_PREFETCH)
    #if !(defined(_MSC_VER) && defined(_WIN32))
        #include <xmmintrin.h>  // _mm_prefetch() intrinsic (SSE)
    #endif
#endif

#include "utility/attributes.h"
#include "utility/types.h"

namespace DON {

using NumaIndex = u16;
using CpuIndex  = u16;

inline constexpr std::string_view EMPTY_STRING{"<empty>"};
inline constexpr std::string_view WHITE_SPACE{" \t\n\v\f\r"};

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

[[nodiscard]] constexpr bool is_idigit(const int dg) noexcept { return 0 <= dg && dg <= 9; }
[[nodiscard]] constexpr bool is_cdigit(const char ch) noexcept { return '0' <= ch && ch <= '9'; }
[[nodiscard]] constexpr bool is_space(const char ch) noexcept {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\v' || ch == '\f' || ch == '\r';
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

void set_console_utf8() noexcept;

constexpr u32 to_month(const std::string_view mon) noexcept {
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

// Tomohiko Sakamoto's Algorithm
constexpr std::string_view week_day(const u32 year, const u32 month, const u32 day) noexcept {
    constexpr Array<std::string_view, 7> Weekdays{
      "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"  //
    };

    // Precomputed weekday offsets for each month.
    constexpr Array<u16, 12> MonthWeekdays{0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

    // Treat January and February as part of the previous year.
    const u32 yr = year - u32(month < 3);

    // Apply the mathematical congruence formula
    const usize weekDay = (yr + yr / 4 - yr / 100 + yr / 400 + MonthWeekdays[month - 1] + day)  //
                        % Weekdays.size();

    return Weekdays[weekDay];
}

// Format date "Mon DD YYYY" -> YYYYMMDD
std::string format_date(std::string_view date) noexcept;
// Format time HH:MM:SS -> HHMMSS
std::string format_time(std::string_view time) noexcept;

std::string build_date() noexcept;

std::string build_time() noexcept;

std::string build_timestamp() noexcept;

std::string engine_info(bool uci = false) noexcept;

std::string engine_logo() noexcept;

// Returns the full human-readable DON version string.
//
// Development builds:
//   • If Git metadata is available, append commit information:
//       DON dev-YYYYMMDD-SHA
//
//   • If Git metadata is unavailable (e.g. local/source builds),
//     fall back to a timestamp-based identifier:
//       DON dev-YYYYMMDD-HHMMSS
//
// Release builds:
//   • Only include the semantic version number:
//       DON X.Y (version)
std::string version_info() noexcept;

// Returns a string trying to describe the compiler used
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
// Preloads the given address into cache as a performance hint.
// Non-blocking hint that allows the CPU to load data before it is accessed.
// NOTE:
// On x86, _mm_prefetch() does NOT truly distinguish READ vs WRITE.
// PrefetchAccess::WRITE is a best-effort hint and may behave identically to READ.
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
        // PrefetchLoc::HIGH
        return _MM_HINT_T0;
    }();

        #if defined(_MSC_VER)
    PreFetchCacheLine(Hint, addr);

        #else
    _mm_prefetch(static_cast<const char*>(addr), Hint);

        #endif

    #else
    __builtin_prefetch(addr, int(Access), int(Loc));

    #endif
}
#else
template<PrefetchAccess Access = PrefetchAccess::READ, PrefetchLoc Loc = PrefetchLoc::HIGH>
inline void prefetch(const void*) noexcept {}
#endif

// Define a custom case-insensitive hash
struct CaseInsensitiveHash final {
    usize operator()(std::string_view sv) const noexcept;
};
// Define a custom case-insensitive equality
struct CaseInsensitiveEqual final {
    bool operator()(std::string_view sv1, std::string_view sv2) const noexcept;
};
// Define a custom case-insensitive less
struct CaseInsensitiveLess final {
    bool operator()(std::string_view sv1, std::string_view sv2) const noexcept;
};

// Wrapper around std::call_once that also tracks whether initialization completed.
struct CallOnce final {
   public:
    CallOnce() noexcept = default;

    // Initialize using the provided function.
    // The function is invoked until one call completes successfully,
    // even if multiple threads call this function.
    template<typename Func>
    void operator()(Func&& callFn) {
        std::call_once(onceFlag, [this, callFunc = std::forward<Func>(callFn)]() mutable {
            std::move(callFunc)();  // Move into the call
            onceDone.store(true, std::memory_order_release);
        });
    }

    // Check if initialization has been completed.
    [[nodiscard]] bool once_done() const noexcept {
        return onceDone.load(std::memory_order_acquire);
    }

   private:
    CallOnce(const CallOnce&) noexcept            = delete;
    CallOnce& operator=(const CallOnce&) noexcept = delete;
    CallOnce(CallOnce&&) noexcept                 = delete;
    CallOnce& operator=(CallOnce&&) noexcept      = delete;

    std::once_flag    onceFlag;
    std::atomic<bool> onceDone{false};
};

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
    // Factory method that creates a FixedText from the specified string view
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

struct CommandLine final {
   public:
    CommandLine(const int argc, const char* const argv[]) noexcept;
    CommandLine(const CommandLine&)            = delete;
    CommandLine& operator=(const CommandLine&) = delete;
    CommandLine(CommandLine&&)                 = default;
    CommandLine& operator=(CommandLine&&)      = default;

    // Returns the directory containing the executable, or "." if the directory is empty.
    // This is useful because, when the NNUE network is not embedded in the binary,
    // this directory is one of the locations to look for the NNUE file.
    static fs::path binary_directory(fs::path path) noexcept;
    // Returns the process's current working directory.
    static fs::path working_directory() noexcept;

    [[nodiscard]] const StringViews& arguments() const noexcept;

   private:
    StringViews arguments_;
#if defined(_WIN32)
    Strings utf8_arguments;
#endif
};

// SyncOS
//
// Provides RAII-based, thread-safe output synchronization for a std::ostream.
//
// Each SyncOS acquires the mutex associated with the given ostream through
// OstreamMutexRegistry and holds the lock for its lifetime.
//
// Key Features:
//  - Thread-safe: synchronizes access to the associated ostream.
//  - RAII-based: acquires the mutex on construction and releases it on destruction.
//  - Move-constructible: allows SyncOS objects to be returned by value.
//  - Non-copyable and non-move-assignable: prevents accidental lock ownership changes.
//  - Supports standard ostream operators and manipulators.
//  - Asserts on use of a moved-from SyncOS in debug builds.
//
// Usage:
//   SyncOS(std::cout) << "Thread-safe message " << value << std::endl;
//
// Notes:
//  - The lock is held for the lifetime of the SyncOS object.
//  - Keep SyncOS objects short-lived to minimize lock contention.
//  - OstreamMutexRegistry ensures that the same ostream uses the same mutex.
class [[nodiscard]] SyncOS final {
   public:
    explicit SyncOS(std::ostream& os) noexcept;

    SyncOS(const SyncOS&) noexcept            = delete;
    SyncOS& operator=(const SyncOS&) noexcept = delete;
    // Move-constructible so SyncOS objects can be returned by value
    SyncOS(SyncOS&& syncOs) noexcept;
    // Move-assignment is deleted to prevent changing lock ownership
    SyncOS& operator=(SyncOS&&) noexcept = delete;

    using IosManip = std::ios& (*) (std::ios&);

    SyncOS&  operator<<(IosManip manip) &;
    SyncOS&& operator<<(IosManip manip) &&;

    using OstreamManip = std::ostream& (*) (std::ostream&);

    SyncOS&  operator<<(OstreamManip manip) &;
    SyncOS&& operator<<(OstreamManip manip) &&;

    template<typename T>
    SyncOS& operator<<(T&& x) & {
        assert(osPtr != nullptr && "Use of moved-from SyncOS");

        *osPtr << std::forward<T>(x);
        return *this;
    }
    template<typename T>
    SyncOS&& operator<<(T&& x) && {
        assert(osPtr != nullptr && "Use of moved-from SyncOS");

        *osPtr << std::forward<T>(x);
        return std::move(*this);
    }

   private:
    std::ostream*                osPtr;
    std::unique_lock<std::mutex> uniqueLock;
};

[[nodiscard]] SyncOS sync_os(std::ostream& os = std::cout) noexcept;

// Wrapper around std::atomic<T> that uses relaxed atomic or plain accesses, depending on the configuration.
// Intended for platforms such as WebAssembly, where the overhead of atomic instructions can be significant
// and only non-tearing accesses are required for the updates, while ensuring we use relaxed accesses otherwise.
template<typename T>
class RelaxedAtomic final {
   public:
    RelaxedAtomic() = default;
    RelaxedAtomic(T v) noexcept { store(v); }

    RelaxedAtomic(const RelaxedAtomic& relaxedAtomic) noexcept {
        store(static_cast<T>(relaxedAtomic));
    }
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

    bool compare_exchange_strong(T& expected, T desired) noexcept {
        if constexpr (UseAtomic)
            return value.compare_exchange_strong(expected, desired, std::memory_order_relaxed,
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
        u64 k0;
        // Safe unaligned load
        std::memcpy(&k0, p, BLOCK_8);

        k0 = mix(k0);
        // Merge block into the running hash
        h ^= k0;
        h *= MurmurM;
    }
    // Handle remaining tail bytes (< 8) at the end
    if (p < end)
    {
        u64 k = 0;

        // Read remaining bytes in little-endian order
        for (usize i = 0; p != end; ++p, ++i)
            k |= u64(*p) << (i * BYTE_BITS);

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
    constexpr usize HashMagic =
#if defined(SIZE_MAX) && SIZE_MAX == UINT64_MAX
      usize{0x9E3779B97F4A7C15}
#else
      usize{0x9E3779B9}
#endif
    ;

    usize x;
    // For primitive types we avoid using the default hasher, which may be
    // nondeterministic across program invocations
    if constexpr (std::is_integral<T>())
        x = v;
    else
        x = std::hash<T>{}(v);

    seed ^= x + HashMagic + (seed << 6) + (seed >> 2);
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

// StringReader: Fast, Small, allocation-free, read-only parser over std::string_view.
class StringReader final {
   public:
    explicit StringReader(std::string_view sv) noexcept;

    // Returns the current character without advancing the reader.
    //
    // Returns Null when the end of the input is reached.
    char peek() const noexcept;

    // Returns true if the current character exists and is not whitespace.
    bool is_not_space() const noexcept;

    // Advances the reader past all consecutive whitespace characters.
    void skip_spaces() noexcept;

    void advance() noexcept;

    // Returns the current character and advances the reader.
    //
    // Returns Null when the end of the input is reached.
    char get() noexcept;

    // Reads a signed integer after skipping leading whitespace.
    //
    // Returns false if no integer is found; otherwise stores the parsed
    // value in 'out' and returns true.
    bool get_int(int& out) noexcept;

    static constexpr char Null = '\0';

   private:
    const char* const beg;
    const char*       cur;
    const char* const end;
};

// Custom streambuf that wraps std::string_view
class StringBuf final: public std::streambuf {
   public:
    explicit StringBuf(std::string_view sv) noexcept;
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
    TieBuf() noexcept = default;
    TieBuf(std::streambuf* pBf, std::streambuf* mBf) noexcept;

   protected:
    // Synchronizes both the primary and mirror buffers.
    int sync() override;
    // Reads the next character from the primary buffer without consuming it.
    int_type underflow() override;
    // Writes one character to the primary buffer and mirrors it with an output prefix.
    int_type overflow(int_type ch) override;
    // Reads and consumes one character from the primary buffer, then mirrors it with an input prefix.
    int_type uflow() override;
    // Writes a block to the primary buffer and mirrors the written characters with an output prefix.
    std::streamsize xsputn(const char_type* s, std::streamsize count) override;

    [[nodiscard]] std::streambuf* pbuf() const noexcept;
    [[nodiscard]] std::streambuf* mbuf() const noexcept;

   private:
    // Mirrors a character to the secondary buffer, adding a prefix at the start of each line.
    int_type
    mirror_put_with_prefix(int_type ch, std::string_view prefix, char_type& preCh) noexcept;

    std::streambuf *pBuf = nullptr, *mBuf = nullptr;

    char_type opreCh = '\n', ipreCh = '\n';
};

// Logger
//
// Manages runtime logging by redirecting std::cin and std::cout through
// TieBuf objects to mirror I/O to a log file.
class Logger final {
   public:
    // Starts logging to the specified file.
    // Returns true on success and false if the log file cannot be opened.
    static bool start(const fs::path& logPath) noexcept;
    // Stops logging, restores the original streams, and closes the log file.
    static void stop() noexcept;

   private:
    Logger() noexcept = delete;
    // Initializes the logger with the streams to be redirected and mirrored.
    Logger(std::istream& isRef, std::ostream& osRef) noexcept;
    // Stops logging and restores the original streams.
    ~Logger() noexcept;
    // Returns the single shared Logger instance.
    static Logger& instance() noexcept;
    // Opens the specified log file and redirects the streams through TieBuf.
    // Caller must hold 'mutex'.
    bool open(const fs::path& logPath) noexcept;
    // Restores the original streams and closes the log file.
    // Caller must hold 'mutex'.
    void close() noexcept;
    // Returns true if the log file is open.
    [[nodiscard]] bool is_open() const noexcept;
    // Writes a timestamped marker to the log file.
    void write_timestamp(std::string_view suffix) noexcept;

    std::mutex      mutex;
    std::ofstream   ofs;
    std::istream&   is;
    std::ostream&   os;
    std::streambuf *isBuf = nullptr, *osBuf = nullptr;
    TieBuf          itBuf, otBuf;
    std::string     filename;
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
    // The needed Windows API for processor groups could be missed from old Windows versions,
    // so instead of calling them directly (forcing the linker to resolve the calls at compile time),
    // try to load them at runtime.
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
    const SIZE_T largePageSize = ::GetLargePageMinimum();

    if (largePageSize == 0)
        return failureFunc();

    assert(is_power_of_2(largePageSize));

    Advapi advapi;

    if (!advapi.load())
        return failureFunc();

    HANDLE processHandle = HANDLE_INVALID;

    HandleGuard processHandleGuard{processHandle};

    // Need SeLockMemoryPrivilege, so try to enable it for the process
    if (!advapi.openProcessToken(::GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
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
    ::SetLastError(ERROR_SUCCESS);

    if (!advapi.adjustTokenPrivileges(processHandle, FALSE, &newTp, sizeof(oldTp), &oldTp,
                                      &oldTpLen)
        || ::GetLastError() != ERROR_SUCCESS)
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

[[nodiscard]] constexpr bool starts_with(const std::string_view sv,
                                         const std::string_view prefix) noexcept {
    return sv.size() >= prefix.size()  //
        && sv.compare(0, prefix.size(), prefix) == 0;
}

[[nodiscard]] constexpr bool ends_with(const std::string_view sv,
                                       const std::string_view suffix) noexcept {
    return sv.size() >= suffix.size()  //
        && sv.compare(sv.size() - suffix.size(), suffix.size(), suffix) == 0;
}

[[nodiscard]] constexpr bool is_whitespace(const std::string_view sv) noexcept {
    return sv.find_first_not_of(WHITE_SPACE) == std::string_view::npos;
}

[[nodiscard]] constexpr std::string_view ltrim(const std::string_view sv) noexcept {
    // Find the first non-whitespace character
    const auto beg = sv.find_first_not_of(WHITE_SPACE);

    if (beg == std::string_view::npos)
        return {};

    return sv.substr(beg);
}

[[nodiscard]] constexpr std::string_view rtrim(const std::string_view sv) noexcept {
    // Find the last non-whitespace character
    const auto end = sv.find_last_not_of(WHITE_SPACE);

    if (end == std::string_view::npos)
        return {};

    return sv.substr(0, end + 1);
}

[[nodiscard]] constexpr std::string_view trim(const std::string_view sv) noexcept {
    const auto beg = sv.find_first_not_of(WHITE_SPACE);

    if (beg == std::string_view::npos)
        return {};

    const auto end = sv.find_last_not_of(WHITE_SPACE);

    return sv.substr(beg, end - beg + 1);
}

[[nodiscard]] constexpr std::string_view bool_to_str(const bool b) noexcept {
    return b ? "true" : "false";
}

[[nodiscard]] constexpr bool str_to_bool(const std::string_view sv) {
    return (trim(sv) == bool_to_str(true));
}

[[nodiscard]] constexpr int str_to_int(const std::string_view sv) noexcept {
    const char* p   = sv.data();
    const char* end = p + sv.size();

    const bool neg = p != end && *p == '-';
    if (p != end && (*p == '+' || *p == '-'))
        ++p;

    int val = 0;
    for (; p != end; ++p)
        val = 10 * val + char_to_digit(*p);

    return neg ? -val : val;
}

std::string lower_case(std::string str) noexcept;

std::string upper_case(std::string str) noexcept;

std::string toggle_case(std::string str) noexcept;

std::string remove_whitespace(std::string str) noexcept;

// Validate boolean string (case-insensitive)
bool str_is_bool(std::string_view sv) noexcept;

bool str_in_range(std::string_view sv, int minValue, int maxValue) noexcept;

StringViews split(std::string_view sv, std::string_view delimiter, bool trimPart = false) noexcept;

Strings to_strings(const StringViews& svs) noexcept;

std::string usize_to_hex(usize value) noexcept;

std::string u64_to_hex(u64 value) noexcept;

std::string u32_to_hex_prefix(u32 value) noexcept;

std::string u64_to_hex_prefix(u64 value) noexcept;

inline bool InfoStopped = false;

void print_info_string(std::string_view infos) noexcept;

[[noreturn]] void terminate_on_critical_error(std::string_view message) noexcept;

// Convert a wide string to UTF-8.
std::string wstring_to_utf8(std::wstring_view wsv) noexcept;

fs::path utf8_to_path(std::string_view path) noexcept;

std::optional<usize> str_to_usize(std::string_view sv) noexcept;

// Reads the file as bytes.
// Returns std::nullopt if the file does not exist.
std::optional<std::string> read_file_to_string(const fs::path& filePath) noexcept;

}  // namespace DON

#endif  // MISC_H_INCLUDED
