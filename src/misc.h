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
#include <fstream>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>

#include "utility/environment.h"

#if defined(X86) && defined(USE_PREFETCH) && (defined(_MSC_VER) || defined(__INTEL_COMPILER))
    #define USE_X86_PREFETCH
#endif

#if defined(_WIN32)
    #include <utility>

    #include "utility/platform_win.h"  // PreFetchCacheLine()
#endif

#if defined(USE_X86_PREFETCH)
    #if !defined(_MSC_VER)
        #include <xmmintrin.h>  // _mm_prefetch() intrinsic (SSE)
    #endif
#endif

#include "utility/attributes.h"
#include "utility/types.h"

namespace DON {

using NumaIndex = u16;
using CpuIndex  = u16;

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


using TimePoint = Ms::rep;  // A value in milliseconds
static_assert(sizeof(TimePoint) == sizeof(i64), "TimePoint size must be 8 bytes");

inline TimePoint now() noexcept {
    return std::chrono::duration_cast<Ms>(SteadyClock::now().time_since_epoch()).count();
}

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
    ::PreFetchCacheLine(Hint, addr);

        #else
    ::_mm_prefetch(static_cast<const char*>(addr), Hint);

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

inline bool InfoStopped = false;

void print_info_string(std::string_view infos) noexcept;

[[noreturn]] void terminate_on_critical_error(std::string_view message) noexcept;

}  // namespace DON

#endif  // MISC_H_INCLUDED
