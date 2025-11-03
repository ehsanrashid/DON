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
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>  // IWYU pragma: keep
// IWYU pragma: no_include <__exception/terminate.h>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
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
inline constexpr std::uint16_t LittleEndianValue = 1;
inline const bool IsLittleEndian = *reinterpret_cast<const char*>(&LittleEndianValue) == 1;

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

    template<typename T>
    sync_ostream& operator<<(T&& x) & noexcept {
        *ostream << std::forward<T>(x);
        return *this;
    }
    template<typename T>
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

inline std::string create_hash_string(std::string_view str) {
    return (std::ostringstream{} << std::hex << std::setfill('0')
                                 << std::hash<std::string_view>{}(str))
      .str();
}

template<typename T>
inline void combine_hash(std::size_t& seed, const T& v) {
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9E3779B9U + (seed << 6) + (seed >> 2);
}

template<>
inline void combine_hash(std::size_t& seed, const std::size_t& v) {
    seed ^= v + 0x9E3779B9U + (seed << 6) + (seed >> 2);
}

template<typename T>
inline std::size_t raw_data_hash(const T& value) {
    return std::hash<std::string_view>{}(
      std::string_view(reinterpret_cast<const char*>(&value), sizeof(value)));
}

template<std::size_t Capacity>
class FixedString {
   public:
    FixedString() :
        length_(0) {
        data_[0] = '\0';
    }

    FixedString(const char* str) {
        size_t len = std::strlen(str);
        if (len > Capacity)
            std::terminate();
        std::memcpy(data_, str, len);
        length_        = len;
        data_[length_] = '\0';
    }

    FixedString(const std::string& str) {
        if (str.size() > Capacity)
            std::terminate();
        std::memcpy(data_, str.data(), str.size());
        length_        = str.size();
        data_[length_] = '\0';
    }

    std::size_t size() const { return length_; }
    std::size_t capacity() const { return Capacity; }

    const char* c_str() const { return data_; }
    const char* data() const { return data_; }

    char& operator[](std::size_t i) { return data_[i]; }

    const char& operator[](std::size_t i) const { return data_[i]; }

    FixedString& operator+=(const char* str) {
        size_t len = std::strlen(str);
        if (length_ + len > Capacity)
            std::terminate();
        std::memcpy(data_ + length_, str, len);
        length_ += len;
        data_[length_] = '\0';
        return *this;
    }

    FixedString& operator+=(const FixedString& other) { return (*this += other.c_str()); }

    operator std::string() const { return std::string(data_, length_); }

    operator std::string_view() const { return std::string_view(data_, length_); }

    template<typename T>
    bool operator==(const T& other) const noexcept {
        return (std::string_view) (*this) == other;
    }

    template<typename T>
    bool operator!=(const T& other) const noexcept {
        return (std::string_view) (*this) != other;
    }

    void clear() {
        length_  = 0;
        data_[0] = '\0';
    }

   private:
    char        data_[Capacity + 1];  // +1 for null terminator
    std::size_t length_;
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
              "mul_hi64(): Failed");

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

[[nodiscard]] constexpr char digit_to_char(int digit) noexcept {
    assert(0 <= digit && digit <= 9 && "digit_to_char: non-digit integer");
    return (0 <= digit && digit <= 9) ? digit + '0' : '\0';
}

[[nodiscard]] constexpr int char_to_digit(char ch) noexcept {
    assert('0' <= ch && ch <= '9' && "char_to_digit: non-digit character");
    return ('0' <= ch && ch <= '9') ? ch - '0' : -1;
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

inline constexpr std::string_view WHITE_SPACE{" \t\n\r\f\v"};

[[nodiscard]] constexpr bool starts_with(std::string_view str, std::string_view prefix) noexcept {
    return str.size() >= prefix.size()  //
        && str.compare(0, prefix.size(), prefix) == 0;
}

[[nodiscard]] constexpr bool ends_with(std::string_view str, std::string_view suffix) noexcept {
    return str.size() >= suffix.size()  //
        && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

[[nodiscard]] constexpr bool is_whitespace(std::string_view str) noexcept {
    //return str.empty()
    //    || std::all_of(str.begin(), str.end(), [](unsigned char ch) { return std::isspace(ch); });
    return str.find_first_not_of(WHITE_SPACE) == std::string_view::npos;
}

[[nodiscard]] constexpr std::string_view ltrim(std::string_view str) noexcept {
    // Find the first non-whitespace character
    const std::size_t beg = str.find_first_not_of(WHITE_SPACE);
    return (beg == std::string_view::npos) ? std::string_view{} : str.substr(beg);
}

[[nodiscard]] constexpr std::string_view rtrim(std::string_view str) noexcept {
    // Find the last non-whitespace character
    const std::size_t end = str.find_last_not_of(WHITE_SPACE);
    return (end == std::string_view::npos) ? std::string_view{} : str.substr(0, end + 1);
}

[[nodiscard]] constexpr std::string_view trim(std::string_view str) noexcept {
    const std::size_t beg = str.find_first_not_of(WHITE_SPACE);
    if (beg == std::string_view::npos)
        return {};
    const std::size_t end = str.find_last_not_of(WHITE_SPACE);
    return str.substr(beg, end - beg + 1);
    //return ltrim(rtrim(str));
}

[[nodiscard]] constexpr std::string_view bool_to_string(bool b) noexcept {
    return std::string_view{b ? "true" : "false"};
}

[[nodiscard]] constexpr bool string_to_bool(std::string_view str) { return (trim(str) == "true"); }

inline StringViews
split(std::string_view str, std::string_view delimiter, bool trimEnabled = false) noexcept {
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
        if (trimEnabled)
            part = trim(part);
        if (!part.empty())
            parts.emplace_back(part);

        beg = end + delimiter.size();
    }

    // Last part
    part = str.substr(beg);
    if (trimEnabled)
        part = trim(part);
    if (!part.empty())
        parts.emplace_back(part);

    return parts;
}

inline std::string u64_to_string(std::uint64_t u64) noexcept {
    std::string str(19, '\0');  // "0x" + 16 hex + '\0'
    std::snprintf(str.data(), str.size(), "0x%016" PRIX64, u64);
    return str;
}

std::size_t str_to_size_t(std::string_view str) noexcept;

std::streamsize get_file_size(std::ifstream& ifstream) noexcept;

// Reads the file as bytes.
// Returns std::nullopt if the file does not exist.
std::optional<std::string> read_file_to_string(std::string_view filePath) noexcept;

}  // namespace DON

template<std::size_t N>
struct std::hash<DON::FixedString<N>> {
    std::size_t operator()(const DON::FixedString<N>& fixStr) const noexcept {
        return std::hash<std::string_view>{}((std::string_view) fixStr);
    }
};

#endif  // #ifndef MISC_H_INCLUDED
