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
#include <atomic>
#include <array>
#include <cassert>
#include <cctype>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>  // IWYU pragma: keep
// IWYU pragma: no_include <__exception/terminate.h>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_MSC_VER) && defined(USE_PREFETCH)
    #include <xmmintrin.h>  // Microsoft header for _mm_prefetch()
#endif

#include "memory.h"

#define STRING_LITERAL(x) #x
#define STRINGIFY(x) STRING_LITERAL(x)

#if defined(__GNUC__) || defined(__clang__)
    #define ALWAYS_INLINE inline __attribute__((always_inline))
#elif defined(_MSC_VER)
    #define ALWAYS_INLINE __forceinline
#else
    // fallback: keep 'inline'
    #define ALWAYS_INLINE inline
#endif

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

class [[nodiscard]] SyncOstream final {
   public:
    explicit SyncOstream(std::ostream& _os) noexcept :
        os(&_os),
        lock(mutex) {}
    SyncOstream(const SyncOstream&) noexcept = delete;
    // Move-constructible so factories can return by value
    SyncOstream(SyncOstream&& syncOs) noexcept :
        os(syncOs.os),
        lock(std::move(syncOs.lock)) {
        syncOs.os = nullptr;
    }

    SyncOstream& operator=(const SyncOstream&) noexcept = delete;
    // Prefer deleting move-assignment to avoid unlock window
    SyncOstream& operator=(SyncOstream&&) noexcept = delete;

    ~SyncOstream() noexcept = default;

    template<typename T>
    SyncOstream& operator<<(T&& x) & noexcept {
        assert(os != nullptr && "Use of moved-from SyncOstream");

        *os << std::forward<T>(x);
        return *this;
    }
    template<typename T>
    SyncOstream&& operator<<(T&& x) && noexcept {
        assert(os != nullptr && "Use of moved-from SyncOstream");

        *os << std::forward<T>(x);
        return std::move(*this);
    }

    using IosManip = std::ios& (*) (std::ios&);
    SyncOstream& operator<<(IosManip manip) & noexcept {
        assert(os != nullptr && "Use of moved-from SyncOstream");

        manip(*os);
        return *this;
    }
    SyncOstream&& operator<<(IosManip manip) && noexcept {
        assert(os != nullptr && "Use of moved-from SyncOstream");

        manip(*os);
        return std::move(*this);
    }

    using OstreamManip = std::ostream& (*) (std::ostream&);
    SyncOstream& operator<<(OstreamManip manip) & noexcept {
        assert(os != nullptr && "Use of moved-from SyncOstream");

        manip(*os);
        return *this;
    }
    SyncOstream&& operator<<(OstreamManip manip) && noexcept {
        assert(os != nullptr && "Use of moved-from SyncOstream");

        manip(*os);
        return std::move(*this);
    }

   private:
    static inline std::mutex mutex;

    std::ostream*                os;
    std::unique_lock<std::mutex> lock;
};

[[nodiscard]] inline SyncOstream sync_os(std::ostream& os = std::cout) noexcept {
    return SyncOstream(os);
}

// --- TableView with size ---
template<typename T>
class TableView final {
   public:
    constexpr TableView() noexcept = default;

    constexpr TableView(T* data, std::size_t size) noexcept :
        _data(data),
        _size(size) {}

    constexpr T*          data() noexcept { return _data; }
    constexpr T*          data() const noexcept { return _data; }
    constexpr std::size_t size() const noexcept { return _size; }

    constexpr T* begin() noexcept { return data(); }
    constexpr T* end() noexcept { return data() + size(); }
    constexpr T* begin() const noexcept { return data(); }
    constexpr T* end() const noexcept { return data() + size(); }

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

// --- TableView with size and count ---
template<typename T>
struct CountTableView final {
    using off_type  = std::uint16_t;
    using size_type = std::uint8_t;

    constexpr CountTableView() noexcept = default;
    constexpr CountTableView(off_type offset, size_type size, size_type count = 0) noexcept :
        _offset(offset),
        _size(size),
        _count(count) {}

    void set(off_type offset, size_type size, size_type count = 0) noexcept {
        _offset = offset;
        _size   = size;
        _count  = count;
    }

    constexpr off_type offset() const noexcept { return _offset; }

    constexpr T*       data(T* const base) noexcept { return base + offset(); }
    constexpr const T* data(const T* const base) const noexcept { return base + offset(); }

    constexpr size_type size() const noexcept { return _size; }
    constexpr size_type count() const noexcept { return _count; }

    constexpr T*       begin(T* const base) noexcept { return data(base); }
    constexpr T*       end(T* const base) noexcept { return data(base) + count(); }
    constexpr const T* begin(const T* const base) const noexcept { return data(base); }
    constexpr const T* end(const T* const base) const noexcept { return data(base) + count(); }

    bool push_back(const T& value, T* const base) noexcept {
        assert(count() < size());
        data(base)[_count++] = value;
        return true;
    }
    bool push_back(T&& value, T* const base) noexcept {
        assert(count() < size());
        data(base)[_count++] = std::move(value);
        return true;
    }

    void pop_back() noexcept {
        assert(count() != 0);
        --_count;
    }

    T& back(T* const base) noexcept {
        assert(count() != 0);
        return data(base)[count() - 1];
    }
    const T& back(const T* const base) const noexcept {
        assert(count() != 0);
        return data(base)[count() - 1];
    }

    T& at(size_type idx, T* const base) noexcept {
        assert(idx < count());
        return data(base)[idx];
    }
    const T& at(size_type idx, const T* const base) const noexcept {
        assert(idx < count());
        return data(base)[idx];
    }

    void clear() noexcept { _count = 0; }

    void count(size_type newCount) noexcept {
        if (newCount > size())
            newCount = size();
        _count = newCount;
    }

   private:
    off_type  _offset = 0;
    size_type _size   = 0;
    size_type _count  = 0;
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

    constexpr MultiArray() noexcept = default;

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

        const std::size_t endIdx = std::min(begIdx + count, size());

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
        assert(begIdx < size());
        assert(endIdx <= size());

        if (endIdx > size())
            endIdx = size();

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
    constexpr FixedVector() noexcept = default;

    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return Capacity; }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return _size; }
    [[nodiscard]] constexpr bool        empty() const noexcept { return size() == 0; }
    [[nodiscard]] constexpr bool        full() const noexcept { return size() == capacity(); }

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

    T& operator[](std::size_t idx) noexcept {
        assert(idx < size());
        assert(size() <= capacity());
        return data()[idx];
    }
    const T& operator[](std::size_t idx) const noexcept {
        assert(idx < size());
        assert(size() <= capacity());
        return data()[idx];
    }

    void size(std::size_t newSize) noexcept {
        if (newSize > capacity())
            newSize = capacity();
        _size = newSize;  // Note: doesn't construct/destroy elements
    }

    T* make_space(std::size_t space) noexcept {
        T* value = &data()[size()];
        _size += space;
        assert(size() <= capacity());
        return value;
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

    FixedString(const char* str) {
        std::size_t strSize = std::strlen(str);
        if (strSize > capacity())
            std::terminate();
        std::memcpy(data(), str, strSize);
        _size = strSize;
        null_terminate();
    }

    FixedString(const std::string& str) {
        if (str.size() > capacity())
            std::terminate();
        std::memcpy(data(), str.data(), str.size());
        _size = str.size();
        null_terminate();
    }

    [[nodiscard]] constexpr std::size_t capacity() noexcept { return Capacity; }

    [[nodiscard]] constexpr std::size_t size() const noexcept { return _size; }
    [[nodiscard]] constexpr bool        empty() const noexcept { return size() == 0; }
    [[nodiscard]] constexpr bool        full() const noexcept { return size() == capacity(); }

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

    FixedString& operator+=(const char* str) {
        std::size_t strSize = std::strlen(str);
        if (size() + strSize > capacity())
            std::terminate();
        std::memcpy(data() + size(), str, strSize);
        _size += strSize;
        null_terminate();
        return *this;
    }
    FixedString& operator+=(const std::string& str) {
        *this += str.data();
        return *this;
    }
    FixedString& operator+=(const FixedString& fixedStr) {
        *this += fixedStr.data();
        return *this;
    }

    operator std::string() const noexcept { return std::string(data(), size()); }
    operator std::string_view() const noexcept { return std::string_view(data(), size()); }

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
    StdArray<char, Capacity + 1> _data;  // +1 for null terminator
    std::size_t                  _size;
};

struct AtomicOnce final {
   public:
    AtomicOnce() = default;

    AtomicOnce(const AtomicOnce& atomOnce) noexcept :
        atomicOnce(atomOnce.atomicOnce.load(std::memory_order_relaxed)) {}
    AtomicOnce& operator=(const AtomicOnce& atomOnce) noexcept {
        atomicOnce.store(atomOnce.atomicOnce.load(std::memory_order_relaxed),
                         std::memory_order_relaxed);
        return *this;
    }

    AtomicOnce(AtomicOnce&&) noexcept            = delete;
    AtomicOnce& operator=(AtomicOnce&&) noexcept = delete;

    // Fast path: returns true if already done
    bool is_done() const noexcept { return atomicOnce.load(std::memory_order_acquire); }

    // Attempt to become the initializing thread
    // Returns true if caller is responsible for initialization
    bool try_start() noexcept {
        bool expected = false;
        return atomicOnce.compare_exchange_strong(expected, true, std::memory_order_acquire,
                                                  std::memory_order_relaxed);
    }

    // Spin-wait until ready
    void wait_until_done() const noexcept {
        while (!atomicOnce.load(std::memory_order_acquire))
            std::this_thread::yield();
    }

    // Mark initialization complete
    void set_done() noexcept { atomicOnce.store(true, std::memory_order_release); }

   private:
    std::atomic<bool> atomicOnce{false};
};

// LazyValue wraps a Value with AtomicOnce for safe lazy initialization
template<typename Value>
struct LazyValue final {
    template<typename... Args>
    Value& init(Args&&... args) noexcept {
        // Fast path: already initialized
        if (atomicOnce.is_done())
            return value;

        if (atomicOnce.try_start())
        {
            // first thread initializes
            value = Value(std::forward<Args>(args)...);

            atomicOnce.set_done();
        }
        else
        {
            // other threads wait until ready
            atomicOnce.wait_until_done();
        }

        return value;
    }

   private:
    Value      value;
    AtomicOnce atomicOnce;
};

// ConcurrentCache: groups (mutex + storage + pre-reserve)
template<typename Key, typename Value>
class ConcurrentCache final {
   public:
    ConcurrentCache(std::size_t reserve = 1024, float loadFactor = 0.75f) noexcept {
        storage.reserve(reserve);
        storage.max_load_factor(loadFactor);
    }

    // Thread-safe access or build
    // Args... are forwarded to Value constructor
    template<typename... Args>
    Value& access_or_build(const Key& key, Args&&... args) noexcept {
        // Fast path: shared (read) lock to access
        {
            std::shared_lock lock(mutex);

            if (auto itr = storage.find(key); itr != storage.end())
                return itr->second->init(std::forward<Args>(args)...);
        }

        // Slow path: exclusive (write) lock to insert new LazyValue if missing
        {
            std::unique_lock lock(mutex);

            auto& entry = storage[key];

            if (!entry)
                entry = std::make_unique<LazyValue<Value>>();

            return entry->init(std::forward<Args>(args)...);
        }
    }

    // Transformer is callable: Value& -> any return type
    // Args... are forwarded to Value constructor
    template<typename Transformer, typename... Args>
    auto
    transform_access_or_build(const Key& key, Transformer&& transformer, Args&&... args) noexcept {
        return std::forward<Transformer>(transformer)(
          access_or_build(key, std::forward<Args>(args)...));
    }

   private:
    std::shared_mutex                                          mutex;
    std::unordered_map<Key, std::unique_ptr<LazyValue<Value>>> storage;
};

template<typename T>
inline void combine_hash(std::size_t& seed, const T& v) noexcept {
    seed ^= std::hash<T>{}(v) + 0x9E3779B9U + (seed << 6) + (seed >> 2);
}

template<>
inline void combine_hash(std::size_t& seed, const std::size_t& v) noexcept {
    seed ^= v + 0x9E3779B9U + (seed << 6) + (seed >> 2);
}

template<typename T>
inline std::size_t raw_data_hash(const T& value) noexcept {
    return std::hash<std::string_view>{}(
      std::string_view(reinterpret_cast<const char*>(&value), sizeof(value)));
}

inline std::string create_hash_string(std::string_view str) noexcept {
    return (std::ostringstream{} << std::hex << std::setfill('0')
                                 << std::hash<std::string_view>{}(str))
      .str();
}

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

using TimePoint = std::chrono::milliseconds::rep;  // A value in milliseconds
static_assert(sizeof(TimePoint) == sizeof(std::int64_t), "TimePoint should be 64-bit");
inline TimePoint now() noexcept {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::string format_time(const std::chrono::system_clock::time_point& timePoint) noexcept;

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
// Can toggle the logging of std::cout and std:cin at runtime whilst preserving
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
    // Start logging to `logFile`. Returns true on success.
    static bool start(std::string_view logFile) noexcept {
        std::scoped_lock lock(instance().mutex);

        return instance().open(logFile);
    }

    // Stop logging. Restores original streams and closes the file.
    static void stop() noexcept {
        std::scoped_lock lock(instance().mutex);

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

    ~Logger() noexcept = default;

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

        std::string time = format_time(std::chrono::system_clock::now());

        ofs << '[' << time << "] " << suffix << std::endl;
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
            std::cerr << "Unable to open Log file: " << filename << std::endl;
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

    std::istream& is;
    std::ostream& os;

    std::streambuf *isBuf = nullptr, *osBuf = nullptr;

    std::ofstream ofs;

    TieStreamBuf iTie, oTie;

    std::string filename;

    std::mutex mutex;
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

inline std::string u32_to_string(std::uint32_t u32) noexcept {
    std::string str(11, '\0');  // "0x" + 8 hex + '\0' => 11 bytes
    std::snprintf(str.data(), str.size(), "0x%08" PRIX32, u32);
    return str;
}
inline std::string u64_to_string(std::uint64_t u64) noexcept {
    std::string str(19, '\0');  // "0x" + 16 hex + '\0' >= 19 bytes
    std::snprintf(str.data(), str.size(), "0x%016" PRIX64, u64);
    return str;
}

std::size_t str_to_size_t(std::string_view str) noexcept;

// Reads the file as bytes.
// Returns std::nullopt if the file does not exist.
std::optional<std::string> read_file_to_string(std::string_view filePath) noexcept;

}  // namespace DON

template<std::size_t N>
struct std::hash<DON::FixedString<N>> {
    std::size_t operator()(const DON::FixedString<N>& fixedStr) const noexcept {
        return std::hash<std::string_view>{}((std::string_view) fixedStr);
    }
};

#endif  // #ifndef MISC_H_INCLUDED
