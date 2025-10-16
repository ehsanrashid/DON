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
#include <cassert>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_MSC_VER) && defined(USE_PREFETCH)
    #include <xmmintrin.h>  // Microsoft header for _mm_prefetch()
#endif

#define STRING_LITERAL(x) #x
#define STRINGIFY(x) STRING_LITERAL(x)

#if defined(__GNUC__) && !defined(__clang__)
    #if __GNUC__ >= 13
        #define ASSUME(cond) __attribute__((assume(cond)))
    #else
        #define ASSUME(cond) ((cond) ? (void) 0 : __builtin_unreachable())
    #endif
#else
    // do nothing for other compilers
    #define ASSUME(cond) ((void) 0)
#endif

namespace DON {

using Strings     = std::vector<std::string>;
using StringViews = std::vector<std::string_view>;

std::string engine_info(bool uci = false) noexcept;
std::string version_info() noexcept;
std::string compiler_info() noexcept;

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
    return Wider(x) * x;
}
// Return the square of a number multiplied by its sign, using a wider type to avoid overflow
template<typename T>
constexpr auto sign_sqr(T x) noexcept {
    static_assert(std::is_arithmetic_v<T>, "Argument must be arithmetic");
    return sign(x) * sqr(x);
}

// True if and only if the binary is compiled on a little-endian machine
constexpr std::uint16_t  LittleEndianValue = 1;
static inline const bool IsLittleEndian = *reinterpret_cast<const char*>(&LittleEndianValue) == 1;

class sync_ostream final {
   public:
    explicit sync_ostream(std::ostream& os) noexcept :
        ostream(&os),
        uniqueLock(mutex) {}
    sync_ostream(const sync_ostream&) noexcept = delete;
    // Move-constructible so factories can return by value
    sync_ostream(sync_ostream&& syncOs) noexcept :
        ostream(syncOs.ostream),
        uniqueLock(std::move(syncOs.uniqueLock)) {}

    sync_ostream& operator=(const sync_ostream&) noexcept = delete;
    // Prefer deleting move-assignment to avoid unlock window.
    sync_ostream& operator=(sync_ostream&&) noexcept = delete;

    ~sync_ostream() noexcept = default;

    template<class T>
    sync_ostream& operator<<(T&& x) & noexcept {
        *ostream << std::forward<T>(x);
        return *this;
    }
    template<class T>
    sync_ostream&& operator<<(T&& x) && noexcept {
        *ostream << std::forward<T>(x);
        return std::move(*this);
    }

    using ostream_manip = std::ostream& (*) (std::ostream&);
    sync_ostream& operator<<(ostream_manip manip) & noexcept {
        manip(*ostream);
        return *this;
    }
    sync_ostream&& operator<<(ostream_manip manip) && noexcept {
        manip(*ostream);
        return std::move(*this);
    }

    using ios_manip = std::ios_base& (*) (std::ios_base&);
    sync_ostream& operator<<(ios_manip manip) & noexcept {
        manip(*ostream);
        return *this;
    }
    sync_ostream&& operator<<(ios_manip manip) && noexcept {
        manip(*ostream);
        return std::move(*this);
    }

   private:
    static inline std::mutex mutex;

    std::ostream*                ostream;
    std::unique_lock<std::mutex> uniqueLock;
};

inline sync_ostream sync_os(std::ostream& os = std::cout) { return sync_ostream(os); }

template<typename T, std::size_t MaxSize>
class FixedVector final {
    static_assert(MaxSize > 0, "MaxSize must be > 0");

   public:
    constexpr FixedVector() noexcept = default;

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return MaxSize; }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return _size; }
    [[nodiscard]] constexpr bool        empty() const noexcept { return size() == 0; }
    [[nodiscard]] constexpr bool        full() const noexcept { return size() == capacity(); }

    constexpr T*       begin() noexcept { return _data; }
    constexpr T*       end() noexcept { return _data + size(); }
    constexpr const T* begin() const noexcept { return _data; }
    constexpr const T* end() const noexcept { return _data + size(); }
    constexpr const T* cbegin() const noexcept { return _data; }
    constexpr const T* cend() const noexcept { return _data + size(); }

    bool push_back(const T& value) noexcept {
        if (size() >= capacity())
            return false;
        _data[_size++] = value;  // copy-assign into pre-initialized slot
        return true;
    }
    bool push_back(T&& value) noexcept {
        if (size() >= capacity())
            return false;
        _data[_size++] = std::move(value);
        return true;
    }
    template<typename... Args>
    bool emplace_back(Args&&... args) noexcept {
        if (size() >= capacity())
            return false;
        _data[_size++] = T(std::forward<Args>(args)...);
        return true;
    }

    constexpr const T& operator[](std::size_t idx) const noexcept {
        assert(idx < size());
        return _data[idx];
    }
    constexpr T& operator[](std::size_t idx) noexcept {
        assert(idx < size());
        return _data[idx];
    }

    bool set_size(std::size_t newSize) noexcept {
        if (newSize > capacity())
            return false;
        _size = newSize;  // Note: doesn't construct/destroy elements
        return true;
    }

    void clear() noexcept { _size = 0; }

   private:
    T           _data[MaxSize]{};
    std::size_t _size{0};
};

constexpr std::uint64_t mul_hi64(std::uint64_t u1, std::uint64_t u2) noexcept {
#if defined(IS_64BIT) && defined(__GNUC__)
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
              "Error in mul_hi64()");

#if defined(USE_PREFETCH)
inline void prefetch(const void* const addr) noexcept {
    #if defined(_MSC_VER)
    _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0);
    #else
    __builtin_prefetch(addr);
    #endif
}
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

void start_logger(std::string_view logFile) noexcept;

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
    explicit constexpr PRNG(std::uint64_t seed) noexcept :
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
    constexpr void jump() noexcept {
        std::uint64_t t = 0;
        for (std::uint8_t m = 0; m < 64; ++m)
        {
            if (0x9E3779B97F4A7C15ULL & (1ULL << m))
                t ^= s;
            rand64();
        }
        s = t;
    }

   private:
    // XORShift64Star algorithm implementation
    constexpr std::uint64_t rand64() noexcept {
        s ^= s >> 12, s ^= s << 25, s ^= s >> 27;
        return 0x2545F4914F6CDD1DULL * s;
    }

    std::uint64_t s{};
};

// XORShift1024Star Pseudo-Random Number Generator
class PRNG1024 final {
   public:
    explicit constexpr PRNG1024(std::uint64_t seed) noexcept {
        assert(seed);

        for (std::size_t i = 0; i < Size; ++i)
            s[i] = seed = 0x9857FB32C9EFB5E4ULL + 0x2545F4914F6CDD1DULL * seed;
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
    constexpr void jump() noexcept {
        std::uint64_t t[Size]{};
        for (const auto jumpMask : JumpMasks)
            for (std::uint8_t m = 0; m < 64; ++m)
            {
                if (jumpMask & (1ULL << m))
                    for (std::size_t i = 0; i < Size; ++i)
                        t[i] ^= s[index(i)];
                rand64();
            }

        for (std::size_t i = 0; i < Size; ++i)
            s[index(i)] = t[i];
    }

   private:
    constexpr std::size_t index(std::size_t k) const noexcept { return (p + k) & (Size - 1); }

    // XORShift1024Star algorithm implementation
    constexpr std::uint64_t rand64() noexcept {
        auto s0 = s[p];
        auto s1 = s[p = index(1)];
        s1 ^= s1 << 31;
        s[p] = s0 ^ s1 ^ (s0 >> 30) ^ (s1 >> 11);
        return 0x106689D45497FDB5ULL * s[p];
    }

    static constexpr std::size_t Size = 16;

    static constexpr std::uint64_t JumpMasks[Size]{
      0x84242F96ECA9C41DULL, 0xA3C65B8776F96855ULL, 0x5B34A39F070B5837ULL, 0x4489AFFCE4F31A1EULL,
      0x2FFEEB0A48316F40ULL, 0xDC2D9891FE68C022ULL, 0x3659132BB12FEA70ULL, 0xAAC17D8EFA43CAB8ULL,
      0xC4CB815590989B13ULL, 0x5EE975283D71C93BULL, 0x691548C86C1BD540ULL, 0x7910C41D10A1E6A5ULL,
      0x0B5FC64563B3E2A8ULL, 0x047F7684E9FC949DULL, 0xB99181F2D8F685CAULL, 0x284600E3F30E38C3ULL};

    std::uint64_t s[Size]{};
    std::size_t   p{0};
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

    static std::string binary_directory(std::string path) noexcept;
    static std::string working_directory() noexcept;

    std::vector<std::string_view> arguments;
};

inline char digit_to_char(int digit) noexcept {
    assert(0 <= digit && digit <= 9);
    return (0 <= digit && digit <= 9) ? '0' + digit : '\0';  // Return null char for invalid digit
}

inline int char_to_digit(char ch) noexcept {
    assert(std::isdigit(ch));
    return std::isdigit(ch) ? ch - '0' : -1;  // Return -1 for non-digit characters
}

inline std::string lower_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char ch) noexcept {
        return std::isupper(ch) ? static_cast<unsigned char>(std::tolower(ch)) : ch;
    });
    return str;
}

inline std::string upper_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char ch) noexcept {
        return std::islower(ch) ? static_cast<unsigned char>(std::toupper(ch)) : ch;
    });
    return str;
}

inline std::string toggle_case(std::string str) noexcept {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char ch) noexcept {
        return std::islower(ch) ? static_cast<unsigned char>(std::toupper(ch))
             : std::isupper(ch) ? static_cast<unsigned char>(std::tolower(ch))
                                : ch;
    });
    return str;
}

inline bool starts_with(std::string_view str, std::string_view prefix) noexcept {
    return str.size() >= prefix.size() && str.substr(0, prefix.size()) == prefix;
}

inline bool ends_with(std::string_view str, std::string_view suffix) noexcept {
    return str.size() >= suffix.size() && str.substr(str.size() - suffix.size()) == suffix;
}

inline bool is_whitespace(std::string_view str) noexcept {
    return str.empty()  //
        || std::all_of(str.begin(), str.end(),
                       [](unsigned char ch) noexcept { return std::isspace(ch) != 0; });
}

inline void remove_whitespace(std::string& str) noexcept {
    str.erase(std::remove_if(str.begin(), str.end(),
                             [](unsigned char ch) noexcept { return std::isspace(ch) != 0; }),
              str.end());
}

inline void trim_leading_whitespace(std::string& str) noexcept {
    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](unsigned char ch) noexcept {
                  return std::isspace(ch) == 0;
              }));
}

inline void trim_trailing_whitespace(std::string& str) noexcept {
    str.erase(std::find_if(str.rbegin(), str.rend(),
                           [](unsigned char ch) noexcept { return std::isspace(ch) == 0; })
                .base(),
              str.end());
}

inline std::string bool_to_string(bool b) noexcept {
    return (std::ostringstream{} << std::boolalpha << b).str();
}

inline bool string_to_bool(std::string_view str) {
    std::istringstream iss{std::string(str)};

    // Try "true"/"false" (case-sensitive, C++ iostream semantics)
    if (bool b{}; (iss >> std::boolalpha >> b))
    {
        iss >> std::ws;  // allow trailing spaces
        if (iss.eof())
            return b;  // clean parse
        // Had extra junk -> fall through to integer retry
    }

    // Retry as integer
    iss.clear();
    iss.seekg(0);  // rewind to start
    if (unsigned long long u{}; iss >> u)
    {
        iss >> std::ws;
        if (iss.eof())
            return u != 0;  // only accept clean numeric tokens
    }

    // Optional: common on/off/yes/no fallbacks
    return false;
}

inline std::string u64_to_string(std::uint64_t u64) noexcept {
    return (std::ostringstream{} << "0x" << std::hex << std::uppercase << std::setfill('0')
                                 << std::setw(16) << u64)
      .str();
}

[[nodiscard]] constexpr std::string_view trim(std::string_view str) noexcept {
    constexpr std::string_view WhiteSpace{" \t\n\r\f\v"};

    // Find the first non-whitespace character
    std::size_t beg = str.find_first_not_of(WhiteSpace);
    if (beg == std::string_view::npos)
        return {};  // All whitespace

    // Find the last non-whitespace character
    std::size_t end = str.find_last_not_of(WhiteSpace);

    return str.substr(beg, end - beg + 1);  // Returns the trimmed substring
}

inline StringViews
split(std::string_view str, std::string_view delimiter, bool doTrim = false) noexcept {
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
        if (doTrim)
            part = trim(part);
        if (!part.empty())
            parts.emplace_back(part);

        beg = end + delimiter.size();
    }

    // Last part
    part = str.substr(beg);
    if (doTrim)
        part = trim(part);
    if (!part.empty())
        parts.emplace_back(part);

    return parts;
}

std::size_t str_to_size_t(std::string_view str) noexcept;

std::streamsize get_file_size(std::ifstream& ifstream) noexcept;

// Reads the file as bytes.
// Returns std::nullopt if the file does not exist.
std::optional<std::string> read_file_to_string(std::string_view filePath) noexcept;

}  // namespace DON

#endif  // #ifndef MISC_H_INCLUDED
