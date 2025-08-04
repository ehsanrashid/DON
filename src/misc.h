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
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>  // IWYU pragma: keep
#include <vector>

#if defined(USE_PREFETCH)
    #if defined(_MSC_VER)
        #include <xmmintrin.h>  // Microsoft header for _mm_prefetch()
    #else
        //#include <xmmintrin.h>
    #endif
#endif

#if !defined(STRINGIFY)
    #define STRING_LITERAL(x) #x
    #define STRINGIFY(x) STRING_LITERAL(x)
#endif

namespace DON {

std::string engine_info(bool uci = false) noexcept;
std::string version_info() noexcept;
std::string compiler_info() noexcept;

template<typename T, std::size_t Size, std::size_t... Sizes>
class MultiArray;

namespace Internal {
template<typename T, std::size_t Size, std::size_t... Sizes>
struct [[maybe_unused]] MultiArrayTypedef;

// Recursive template to define multi-dimensional MultiArray
template<typename T, std::size_t Size, std::size_t... Sizes>
struct MultiArrayTypedef final {
    using Type = MultiArray<T, Sizes...>;
};
// Base case: single-dimensional MultiArray
template<typename T, std::size_t Size>
struct MultiArrayTypedef<T, Size> final {
    using Type = T;
};
}  // namespace Internal

// MultiArray is a generic N-dimensional array.
// The first template parameter T is the base type of the MultiArray
// The template parameters (Size and Sizes) encode the dimensions of the array.
template<typename T, std::size_t Size, std::size_t... Sizes>
class MultiArray final {
   public:
    using Array = std::array<typename Internal::MultiArrayTypedef<T, Size, Sizes...>::Type, Size>;

    using value_type             = typename Array::value_type;
    using size_type              = typename Array::size_type;
    using difference_type        = typename Array::difference_type;
    using reference              = typename Array::reference;
    using const_reference        = typename Array::const_reference;
    using pointer                = typename Array::pointer;
    using const_pointer          = typename Array::const_pointer;
    using iterator               = typename Array::iterator;
    using const_iterator         = typename Array::const_iterator;
    using reverse_iterator       = typename Array::reverse_iterator;
    using const_reverse_iterator = typename Array::const_reverse_iterator;

    constexpr auto begin() const noexcept { return array.begin(); }
    constexpr auto end() const noexcept { return array.end(); }
    constexpr auto begin() noexcept { return array.begin(); }
    constexpr auto end() noexcept { return array.end(); }

    constexpr auto cbegin() const noexcept { return array.cbegin(); }
    constexpr auto cend() const noexcept { return array.cend(); }

    constexpr auto rbegin() const noexcept { return array.rbegin(); }
    constexpr auto rend() const noexcept { return array.rend(); }
    constexpr auto rbegin() noexcept { return array.rbegin(); }
    constexpr auto rend() noexcept { return array.rend(); }

    constexpr auto crbegin() const noexcept { return array.crbegin(); }
    constexpr auto crend() const noexcept { return array.crend(); }

    constexpr auto&       front() noexcept { return array.front(); }
    constexpr const auto& front() const noexcept { return array.front(); }
    constexpr auto&       back() noexcept { return array.back(); }
    constexpr const auto& back() const noexcept { return array.back(); }

    auto*       data() { return array.data(); }
    const auto* data() const { return array.data(); }

    constexpr auto max_size() const noexcept { return array.max_size(); }

    constexpr auto size() const noexcept { return array.size(); }
    constexpr auto empty() const noexcept { return array.empty(); }

    constexpr const auto& at(size_type idx) const noexcept { return array.at(idx); }
    constexpr auto&       at(size_type idx) noexcept { return array.at(idx); }

    constexpr auto& operator[](size_type idx) const noexcept { return array[idx]; }
    constexpr auto& operator[](size_type idx) noexcept { return array[idx]; }

    constexpr void swap(MultiArray<T, Size, Sizes...>& multiArray) noexcept {
        array.swap(multiArray.array);
    }

    // Recursively fill all dimensions by calling the sub fill method
    template<typename U>
    void fill(U v) noexcept {
        static_assert(std::is_assignable_v<T&, U>
                        && (std::is_same_v<T, U> || !std::is_convertible_v<U, T>),
                      "Cannot assign fill value to entry type");

        for (auto& entry : *this)
        {
            if constexpr (sizeof...(Sizes) == 0)
                entry = v;
            else
                entry.fill(v);
        }
    }

    /*
    void print() const noexcept {
        std::cout << Size << ':' << sizeof...(Sizes) << std::endl;
        for (auto& entry : *this)
        {
            if constexpr (sizeof...(Sizes) == 0)
                std::cout << entry << ' ';
            else
                entry.print();
        }
        std::cout << std::endl;
    }
    */

   private:
    Array array;
};

// Return the sign of a number (-1, 0, +1)
template<typename T>
constexpr int sign(T x) noexcept {
    //static_assert(std::is_arithmetic_v<T>, "Argument must be an arithmetic type");
    return (T(0) < x) - (x < T(0));  // Returns 1 for positive, -1 for negative, and 0 for zero
}

template<typename T>
constexpr auto sqr(T x) noexcept {
    static_assert(std::is_arithmetic_v<T>, "Argument must be an arithmetic type");
    using Wider = std::conditional_t<std::is_integral_v<T>, long long, T>;
    return Wider(x) * x;
}

template<typename T>
constexpr auto sign_sqr(T x) noexcept {
    static_assert(std::is_arithmetic_v<T>, "Argument must be an arithmetic type");
    return sign(x) * sqr(x);
}

// True if and only if the binary is compiled on a little-endian machine
constexpr std::uint16_t  LittleEndianValue = 1;
static const inline bool IsLittleEndian = *reinterpret_cast<const char*>(&LittleEndianValue) == 1;

constexpr std::uint64_t mul_hi64(std::uint64_t u1, std::uint64_t u2) noexcept {
#if defined(IS_64BIT) && defined(__GNUC__)
    __extension__ using uint128_t = unsigned __int128;
    return (uint128_t(u1) * uint128_t(u2)) >> 64;
#else
    std::uint64_t u1L = std::uint32_t(u1), u1H = u1 >> 32;
    std::uint64_t u2L = std::uint32_t(u2), u2H = u2 >> 32;
    std::uint64_t mid = u1H * u2L + ((u1L * u2L) >> 32);
    return u1H * u2H + ((u1L * u2H + std::uint32_t(mid)) >> 32) + (mid >> 32);
#endif
}

static_assert(mul_hi64(0xDEADBEEFDEADBEEFull, 0xCAFEBABECAFEBABEull) == 0xB092AB7CE9F4B259ull);

#if defined(USE_PREFETCH)
    #if defined(_MSC_VER)
inline void prefetch(const void* const addr) noexcept {
    _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0);
}
    #else
inline void prefetch(const void* const addr) noexcept { __builtin_prefetch(addr); }
    #endif
#else
inline void prefetch(const void* const) noexcept {}
#endif

using SystemClock  = std::chrono::system_clock;
using SteadyClock  = std::chrono::steady_clock;
using MilliSeconds = std::chrono::milliseconds;
using MicroSeconds = std::chrono::microseconds;

using TimePoint = MilliSeconds::rep;  // A value in milliseconds
static_assert(sizeof(TimePoint) == sizeof(std::int64_t), "TimePoint should be 64 bits");
inline TimePoint now() noexcept {
    return std::chrono::duration_cast<MilliSeconds>(SteadyClock::now().time_since_epoch()).count();
}

std::string format_time(const SystemClock::time_point& timePoint);

void start_logger(const std::string& logFile) noexcept;

// XORShift64Star Pseudo-Random Number Generator
// This class is based on original code written and dedicated
// to the public domain by Sebastiano Vigna (2014).
// It has the following characteristics:
//
//  - Outputs 64-bit numbers
//  - Passes Dieharder and SmallCrush test batteries
//  - Does not require warm-up, no zero-land to escape
//  - Internal state is a single 64-bit integer
//  - Period is 2^64 - 1
//  - Speed: 1.60 ns/call (measured on a Core i7 @3.40GHz)
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
    constexpr T rand() noexcept {
        return T(rand64());
    }

    // Special generator used to fast init magic numbers.
    // Output values only have 1/8th of their bits set on average.
    template<typename T>
    constexpr T sparse_rand() noexcept {
        return T(rand64() & rand64() & rand64());
    }

    // Jump function for the XORShift64Star PRNG
    void jump() noexcept {
        static constexpr std::uint64_t JumpMask = 0x9E3779B97F4A7C15ull;

        std::uint64_t t = 0;
        for (std::uint8_t m = 0; m < 64; ++m)
        {
            if (JumpMask & (1ull << m))
                t ^= s;
            rand64();
        }
        s = t;
    }

   private:
    // XORShift64Star algorithm implementation
    constexpr std::uint64_t rand64() noexcept {
        s ^= s >> 12, s ^= s << 25, s ^= s >> 27;
        return 0x2545F4914F6CDD1Dull * s;
    }

    std::uint64_t s;
};

// XORShift1024Star Pseudo-Random Number Generator
class PRNG1024 final {
   public:
    explicit PRNG1024(std::uint64_t seed) noexcept :
        p(0) {
        assert(seed);

        for (auto& e : s)
            e = seed = 0x9857FB32C9EFB5E4ull + 0x2545F4914F6CDD1Dull * seed;
    }

    template<typename T>
    constexpr T rand() noexcept {
        return T(rand64());
    }

    // Special generator used to fast init magic numbers.
    // Output values only have 1/8th of their bits set on average.
    template<typename T>
    constexpr T sparse_rand() noexcept {
        return T(rand64() & rand64() & rand64());
    }

    // Jump function for the XORShift1024Star PRNG
    void jump() noexcept {
        static constexpr std::array<std::uint64_t, Size> JumpMask{
          // clang-format off
          0x84242F96ECA9C41Dull, 0xA3C65B8776F96855ull, 0x5B34A39F070B5837ull, 0x4489AFFCE4F31A1Eull,
          0x2FFEEB0A48316F40ull, 0xDC2D9891FE68C022ull, 0x3659132BB12FEA70ull, 0xAAC17D8EFA43CAB8ull,
          0xC4CB815590989B13ull, 0x5EE975283D71C93Bull, 0x691548C86C1BD540ull, 0x7910C41D10A1E6A5ull,
          0x0B5FC64563B3E2A8ull, 0x047F7684E9FC949Dull, 0xB99181F2D8F685CAull, 0x284600E3F30E38C3ull
          // clang-format on
        };

        std::array<std::uint64_t, Size> t{};
        for (const auto jumpMask : JumpMask)
            for (std::uint8_t m = 0; m < 64; ++m)
            {
                if (jumpMask & (1ull << m))
                    for (std::size_t i = 0; i < t.size(); ++i)
                        t[i] ^= s[index(i)];
                rand64();
            }

        for (std::size_t i = 0; i < t.size(); ++i)
            s[index(i)] = t[i];
    }

   private:
    constexpr std::size_t index(std::size_t k) const noexcept { return (p + k) & (s.size() - 1); }

    // XORShift1024Star algorithm implementation
    constexpr std::uint64_t rand64() noexcept {
        auto s0 = s[p];
        auto s1 = s[p = index(1)];
        s1 ^= s1 << 31;
        s[p] = s0 ^ s1 ^ (s0 >> 30) ^ (s1 >> 11);
        return 0x106689D45497FDB5ull * s[p];
    }

    static constexpr std::size_t Size = 16;

    std::array<std::uint64_t, Size> s;
    std::size_t                     p;
};

#if !defined(NDEBUG)
namespace Debug {

void init() noexcept;
void reset() noexcept;
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
    CommandLine(int ac, const char** av) noexcept :
        argc(ac),
        argv(av) {}

    static std::string binary_directory(std::string path) noexcept;
    static std::string working_directory() noexcept;

    const int    argc;
    const char** argv;
};


inline char digit_to_char(int digit) noexcept {
    assert(0 <= digit && digit <= 9);
    return '0' + digit;
}
inline int char_to_digit(char ch) noexcept {
    assert(std::isdigit(ch));
    return ch - '0';
}

inline std::string lower_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(), [](char ch) noexcept {
        if (std::isupper(ch))
            return char(std::tolower(ch));
        return ch;
    });
    return str;
}

inline std::string upper_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(), [](char ch) noexcept {
        if (std::islower(ch))
            return char(std::toupper(ch));
        return ch;
    });
    return str;
}

inline std::string toggle_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(), [](char ch) noexcept {
        if (std::islower(ch))
            return char(std::toupper(ch));
        if (std::isupper(ch))
            return char(std::tolower(ch));
        return ch;
    });
    return str;
}

inline bool starts_with(std::string_view str, std::string_view prefix) noexcept {
    return str.size() >= prefix.size()  //
        && str.compare(0, prefix.size(), prefix) == 0;
}

inline bool ends_with(std::string_view str, std::string_view suffix) noexcept {
    return str.size() >= suffix.size()  //
        && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool is_whitespace(std::string_view str) noexcept {
    return str.empty()
        || std::all_of(str.begin(), str.end(), [](char ch) { return std::isspace(ch); });
}

inline void remove_whitespace(std::string& str) noexcept {
    str.erase(  //
      std::remove_if(str.begin(), str.end(), [](char ch) { return std::isspace(ch); }), str.end());
}

inline void trim_leading_whitespace(std::string& str) noexcept {
    str.erase(  //
      str.begin(), std::find_if(str.begin(), str.end(), [](char ch) { return !std::isspace(ch); }));
}

inline void trim_trailing_whitespace(std::string& str) noexcept {
    str.erase(  //
      std::find_if(str.rbegin(), str.rend(), [](char ch) { return !std::isspace(ch); }).base(),
      str.end());
}

inline std::string bool_to_string(bool b) noexcept {
    std::ostringstream oss;
    oss << std::boolalpha << b;
    return oss.str();
}

inline bool string_to_bool(const std::string& str) noexcept {
    bool               b = false;
    std::istringstream iss(lower_case(str));
    iss >> std::boolalpha >> b;
    return b;
}

inline std::string u64_to_string(std::uint64_t u64) noexcept {
    std::ostringstream oss;
    oss << std::setw(16) << std::hex << std::uppercase << std::setfill('0') << u64;
    return oss.str();
}

constexpr std::string_view trim(std::string_view str) noexcept {
    // Define whitespace characters
    constexpr std::string_view Whitespace = " \t\r\n";

    // Find first non-whitespace character
    std::size_t beg = str.find_first_not_of(Whitespace);
    if (beg == std::string_view::npos)
        return {};  // All whitespace

    // Find last non-whitespace character
    std::size_t end = str.find_last_not_of(Whitespace);
    return str.substr(beg, end - beg + 1);
}

inline std::vector<std::string_view>
split(std::string_view str, std::string_view delimiter, bool doTrim = false) noexcept {
    std::vector<std::string_view> parts;

    if (str.empty())
        return parts;

    std::size_t      beg = 0;
    std::string_view part;
    while (true)
    {
        std::size_t end = str.find(delimiter, beg);
        if (end == std::string_view::npos)
            break;

        part = str.substr(beg, end - beg);
        if (doTrim)
            part = trim(part);
        if (!part.empty())
            parts.emplace_back(part);
        beg = end + delimiter.size();
    }
    part = str.substr(beg);
    if (doTrim)
        part = trim(part);
    if (!part.empty())
        parts.emplace_back(part);

    return parts;
}

std::size_t str_to_size_t(const std::string& str) noexcept;

std::streamsize get_file_size(std::ifstream& ifstream) noexcept;

// Reads the file as bytes.
// Returns std::nullopt if the file does not exist.
std::optional<std::string> read_file_to_string(const std::string& filePath) noexcept;

}  // namespace DON

#endif  // #ifndef MISC_H_INCLUDED
