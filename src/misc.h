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
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>  // for std::common_type and std::is_arithmetic
#include <vector>
#if defined(_MSC_VER) && defined(USE_PREFETCH)
    #include <xmmintrin.h>  // Microsoft header for _mm_prefetch()
#endif

#if !defined(STRINGIFY)
    #define STRINGIFY2(x) #x
    #define STRINGIFY(x) STRINGIFY2(x)
#endif

#if defined(__clang__)
    #define FORCE_INLINE [[gnu::always_inline]] [[gnu::gnu_inline]]

#elif defined(__GNUC__)
    #define FORCE_INLINE [[gnu::always_inline]]

#elif defined(_MSC_VER)
    #pragma warning(error: 4714)
    #define FORCE_INLINE __forceinline

#endif

namespace std {
template<typename T, size_t M, size_t N>
using array2d = array<array<T, N>, M>;
}

namespace DON {

std::string engine_info(bool uci = false) noexcept;
std::string compiler_info() noexcept;

//inline void log_call(const std::string& function, const std::string& file, int line) {
//    std::cerr << "Function: " << function << " called from file: " << file
//              << " at line: " << line << std::endl;
//}

//#define LOG_CALL() log_call(__FUNCTION__, __FILE__, __LINE__)

enum OutState : std::uint8_t {
    OUT_LOCK,
    OUT_UNLOCK
};

std::ostream& operator<<(std::ostream& os, OutState out) noexcept;

#define sync_cout std::cout << OUT_LOCK
#define sync_endl std::endl << OUT_UNLOCK
#define sync_end OUT_UNLOCK

template<typename T>
constexpr auto sqr(T x) noexcept -> typename std::common_type<T, long long>::type {
    static_assert(std::is_arithmetic<T>::value, "Template argument must be an arithmetic type");

    using ResultType = typename std::common_type<T, long long>::type;

    ResultType result = ResultType(x) * x;

    // Check for overflow
    assert(result <= std::numeric_limits<T>::max());
    //if (result > std::numeric_limits<T>::max())
    //    std::cerr << "Warning: Result exceeds the range of the original type!\n";
    return result;
}

constexpr std::uint64_t mul_hi64(std::uint64_t a, std::uint64_t b) noexcept {
#if defined(IS_64BIT) && defined(__GNUC__)
    __extension__ using uint128_t = unsigned __int128;
    return uint128_t(a) * uint128_t(b) >> 64;
#else
    std::uint64_t aL = std::uint32_t(a), aH = a >> 32;
    std::uint64_t bL = std::uint32_t(b), bH = b >> 32;
    std::uint64_t c1 = aH * bL + (aL * bL >> 32);  // c0
    std::uint64_t c2 = aL * bH + std::uint32_t(c1);
    return aH * bH + (c2 >> 32) + (c1 >> 32);
#endif
}

// Preloads the given address in L1/L2 cache.
// This is a non-blocking function that doesn't stall the CPU
// waiting for data to be loaded from memory, which can be quite slow.
enum PrefetchHint : std::uint8_t {
    P_HINT_NTA = 0,  // Prefetch data with a Non-temporal hint
    P_HINT_T2  = 1,  // Prefetch into level 3 (L3) cache and higher
    P_HINT_T1  = 2,  // Prefetch into level 2 (L2) cache and higher
    P_HINT_T0  = 3,  // Prefetch into all levels of the cache
    // P_HINT_ET is P_HINT_T with 3rd bit set
    P_HINT_ET2 = 5,
    P_HINT_ET1 = 6,
    P_HINT_ET0 = 7,
    P_HINT_IT1 = 18,
    P_HINT_IT0 = 19,
};

#if !defined(_MSC_VER)
enum PrefetchRW : std::uint8_t {
    P_RW_READ  = 0,
    P_RW_WRITE = 1,
    P_RW_MASK  = 0x1,
};

enum PrefetchLocality : std::uint8_t {
    P_LOCALITY_NONE     = 0,
    P_LOCALITY_LOW      = 1,
    P_LOCALITY_MODERATE = 2,
    P_LOCALITY_HIGH     = 3,
    P_LOCALITY_MASK     = 0x3,
};
#endif

template<PrefetchHint Hint = P_HINT_T0>
inline void prefetch([[maybe_unused]] const void* const addr) noexcept {

#if defined(USE_PREFETCH)
    #if defined(_MSC_VER)
    _mm_prefetch(reinterpret_cast<const char*>(addr), Hint);
    #else
    __builtin_prefetch(addr, (Hint >> 2) & P_RW_MASK, Hint & P_LOCALITY_MASK);
    #endif
#endif
}

using SystemClock  = std::chrono::system_clock;
using SteadyClock  = std::chrono::steady_clock;
using MilliSeconds = std::chrono::milliseconds;
using MicroSeconds = std::chrono::microseconds;

using TimePoint = MilliSeconds::rep;  // A value in milliseconds
static_assert(sizeof(TimePoint) == sizeof(std::int64_t), "TimePoint should be 64 bits");
inline TimePoint now() noexcept {
    return std::chrono::duration_cast<MilliSeconds>(SteadyClock::now().time_since_epoch()).count();
}

std::string format_time(std::chrono::time_point<SystemClock> timePoint);

void start_logger(const std::string& logFile) noexcept;

// XORShift64Star Pseudo-Random Number Generator
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
   public:
    explicit PRNG(std::uint64_t seed) noexcept :
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

    // Jump function for the XORShift64Star PRNG
    void jump() noexcept {
        constexpr std::uint64_t JUMP_MASK = 0x9E3779B97F4A7C15ull;

        std::uint64_t t    = 0;
        std::uint64_t mask = 1ull;
        while (mask)
        {
            if (JUMP_MASK & mask)
                t ^= s;
            rand64();
            mask <<= 1;
        }
        s = t;
    }

   private:
    // XORShift64Star algorithm implementation
    std::uint64_t rand64() noexcept {
        constexpr std::uint64_t SEED_MULTIPLIER = 0x2545F4914F6CDD1Dull;

        s ^= s >> 12, s ^= s << 25, s ^= s >> 27;
        return SEED_MULTIPLIER * s;
    }

    std::uint64_t s;
};

// XORShift1024Star Pseudo-Random Number Generator
class PRNG1024 final {
   public:
    explicit PRNG1024(std::uint64_t seed) noexcept :
        p(0) {
        constexpr std::uint64_t SEED_OFFSET     = 0x9857FB32C9EFB5E4ull;
        constexpr std::uint64_t SEED_MULTIPLIER = 0x2545F4914F6CDD1Dull;
        assert(seed);

        for (std::size_t i = 0; i < SEED_SIZE; ++i)
            s[i] = seed = SEED_OFFSET + SEED_MULTIPLIER * seed;
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

    // Jump function for the XORShift1024Star PRNG
    void jump() noexcept {
        // clang-format off
        constexpr std::array<std::uint64_t, SEED_SIZE> JUMP_MASK{
          0x84242F96ECA9C41Dull, 0xA3C65B8776F96855ull, 0x5B34A39F070B5837ull, 0x4489AFFCE4F31A1Eull,
          0x2FFEEB0A48316F40ull, 0xDC2D9891FE68C022ull, 0x3659132BB12FEA70ull, 0xAAC17D8EFA43CAB8ull,
          0xC4CB815590989B13ull, 0x5EE975283D71C93Bull, 0x691548C86C1BD540ull, 0x7910C41D10A1E6A5ull,
          0x0B5FC64563B3E2A8ull, 0x047F7684E9FC949Dull, 0xB99181F2D8F685CAull, 0x284600E3F30E38C3ull};
        // clang-format on

        std::array<std::uint64_t, SEED_SIZE> t{};
        for (std::size_t i = 0; i < SEED_SIZE; ++i)
        {
            std::uint64_t mask = 1ull;
            while (mask)
            {
                if (JUMP_MASK[i] & mask)
                    for (std::size_t j = 0; j < SEED_SIZE; ++j)
                        t[j] ^= s[get_index(j + p)];
                rand64();
                mask <<= 1;
            }
        }
        for (std::size_t i = 0; i < SEED_SIZE; ++i)
            s[get_index(i + p)] = t[i];
    }

   private:
    static constexpr std::size_t get_index(std::size_t k) noexcept { return k & (SEED_SIZE - 1); }

    // XORShift1024Star algorithm implementation
    std::uint64_t rand64() noexcept {
        constexpr std::uint64_t SEED_MULTIPLIER = 0x106689D45497FDB5ull;

        std::uint64_t s0 = s[p];
        std::uint64_t s1 = s[p = get_index(1 + p)];
        s1 ^= s1 << 31;
        s[p] = s0 ^ s1 ^ (s0 >> 30) ^ (s1 >> 11);
        return SEED_MULTIPLIER * s[p];
    }

    static constexpr std::size_t SEED_SIZE = 16;

    std::array<std::uint64_t, SEED_SIZE> s;
    std::size_t                          p;
};

#if defined(__linux__)

struct PipeDeleter final {
    void operator()(FILE* pFile) const {
        if (pFile != nullptr)
            pclose(pFile);
    }
};

#endif

#if !defined(NDEBUG)
namespace Debug {
void init() noexcept;
void hit_on(bool cond, std::uint8_t slot = 0) noexcept;
void min_of(std::int64_t value, std::uint8_t slot = 0) noexcept;
void max_of(std::int64_t value, std::uint8_t slot = 0) noexcept;
void extreme_of(std::int64_t value, std::uint8_t slot = 0) noexcept;
void mean_of(std::int64_t value, std::uint8_t slot = 0) noexcept;
void stdev_of(std::int64_t value, std::uint8_t slot = 0) noexcept;
void correl_of(std::int64_t value1, std::int64_t value2, std::uint8_t slot = 0) noexcept;
void print() noexcept;
}  // namespace Debug
#endif

// True if and only if the binary is compiled on a little-endian machine
static inline const union {
    std::uint32_t i;
    char          c[4];
} Le                                    = {0x01020304};
static inline const bool IsLittleEndian = (Le.c[0] == 4);

struct CommandLine final {
    CommandLine(int ac, const char** av) noexcept :
        argc(ac),
        argv(av) {}

    static std::string get_binary_directory(const std::string& path) noexcept;
    static std::string get_working_directory() noexcept;

    int          argc;
    const char** argv;
};

constexpr inline std::string_view EMPTY_STRING{"<empty>"};

inline std::string to_lower(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(), [](char ch) noexcept -> char {
        if (std::isupper(ch))
            return char(std::tolower(ch));
        return ch;
    });
    return str;
}

inline std::string to_upper(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(), [](char ch) noexcept -> char {
        if (std::islower(ch))
            return char(std::toupper(ch));
        return ch;
    });
    return str;
}

inline std::string toggle_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(), [](char ch) noexcept -> char {
        if (std::islower(ch))
            return char(std::toupper(ch));
        if (std::isupper(ch))
            return char(std::tolower(ch));
        return ch;
    });
    return str;
}

inline bool starts_with(const std::string& str, const std::string& prefix) noexcept {
    //return str.starts_with(prefix);  // C++20
    return str.size() >= prefix.size()
        //&& strncmp(str.c_str(), prefix.c_str(), prefix.size()) == 0;
        //&& str.substr(0, prefix.size()) == prefix;
        //&& std::mismatch(prefix.begin(), prefix.end(), str.begin()).first == prefix.end();
        //&& std::equal(prefix.begin(), prefix.end(), str.begin());
        //&& str.rfind(prefix, 0) == 0;
        //&& str.find(prefix) == 0;
        && str.compare(0, prefix.size(), prefix) == 0;
}

inline bool ends_with(const std::string& str, const std::string& suffix) noexcept {
    //return str.ends_with(suffix);  // C++20
    return str.size() >= suffix.size()
        //&& strncmp(str.c_str() + str.size() - suffix.size(), suffix.c_str(), suffix.size()) == 0;
        //&& str.substr(str.size() - suffix.size(), suffix.size()) == suffix;
        //&& std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
        //&& str.find(suffix, str.size() - suffix.size()) == (str.size() - suffix.size());
        //&& str.rfind(suffix) == (str.size() - suffix.size());
        && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool is_whitespace(const std::string& str) noexcept {
    return str.empty() || std::all_of(str.begin(), str.end(), ::isspace);
}

inline bool is_empty(const std::string& str) noexcept {
    return is_whitespace(str) || to_lower(str) == EMPTY_STRING;
}

inline void remove_whitespace(std::string& str) noexcept {
    str.erase(std::remove_if(str.begin(), str.end(), ::isspace), str.end());
}

inline void trim_leading_whitespace(std::string& str) noexcept {
    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](char ch) noexcept -> bool {
                  return !std::isspace(ch);
              }));
}

inline void trim_trailing_whitespace(std::string& str) noexcept {
    str.erase(std::find_if(str.rbegin(), str.rend(),
                           [](char ch) noexcept -> bool { return !std::isspace(ch); })
                .base(),
              str.end());
}

inline bool is_valid_bool(std::string& str) noexcept {
    auto s = to_lower(str);
    if (s == "true" || s == "false")
    {
        str = s;
        return true;
    }
    return false;
}

inline std::string bool_to_string(bool b) noexcept {
    std::ostringstream oss;
    oss << std::boolalpha << b;
    return oss.str();
}

inline bool string_to_bool(const std::string& str) noexcept {
    bool b = false;

    std::istringstream iss(to_lower(str));
    iss >> std::boolalpha >> b;
    return b;
}

inline std::string u64_to_string(std::uint64_t u64) noexcept {
    std::ostringstream oss;
    oss << std::setw(16) << std::hex << std::uppercase << std::setfill('0') << u64;
    return oss.str();
}

inline std::vector<std::string> split(const std::string& str,
                                      const std::string& delimiter) noexcept {
    std::vector<std::string> vec;

    if (str.empty())
        return vec;

    std::size_t begin = 0;
    while (true)
    {
        std::size_t end = str.find(delimiter, begin);
        if (end == std::string::npos)
            break;

        vec.emplace_back(str.substr(begin, end - begin));
        begin = end + delimiter.size();
    }
    vec.emplace_back(str.substr(begin));

    return vec;
}

std::size_t str_to_size_t(const std::string& str) noexcept;

// Reads the file as bytes.
// Returns std::nullopt if the file does not exist.
std::optional<std::string> read_file_to_string(const std::string& filePath) noexcept;

}  // namespace DON

#endif  // #ifndef MISC_H_INCLUDED
