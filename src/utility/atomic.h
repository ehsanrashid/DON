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

#ifndef UTILITY_ATOMIC_H_INCLUDED
#define UTILITY_ATOMIC_H_INCLUDED

#include <atomic>
#include <type_traits>

namespace DON {

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
        add(T{1});
        return *this;
    }
    RelaxedAtomic& operator--() noexcept {
        sub(T{1});
        return *this;
    }

    T operator++(int) noexcept { return add(T{1}); }
    T operator--(int) noexcept { return sub(T{1}); }

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
        /*
        if constexpr (std::is_arithmetic_v<T>)
        {
            if constexpr (UseAtomic)
                return value.fetch_add(v, std::memory_order_relaxed);
            else
            {
                T oldV = value;
                value += v;
                return oldV;
            }
        }
        else
        {
            static_assert(std::is_arithmetic_v<T>,
                          "Arithmetic operators require an arithmetic type");
            return T{};
        }
        */

        const T oldV = load();
        const T newV = oldV + v;
        store(newV);
        return oldV;
    }

    T sub(T v) noexcept {
        /*
        if constexpr (std::is_arithmetic_v<T>)
        {
            if constexpr (UseAtomic)
                return value.fetch_sub(v, std::memory_order_relaxed);
            else
            {
                T oldV = value;
                value -= v;
                return oldV;
            }
        }
        else
        {
            static_assert(std::is_arithmetic_v<T>,
                          "Arithmetic operators require an arithmetic type");
            return T{};
        }
        */

        const T oldV = load();
        const T newV = oldV - v;
        store(newV);
        return oldV;
    }

    std::conditional_t<UseAtomic, std::atomic<T>, T> value;
};

}  // namespace DON

#endif  // UTILITY_ATOMIC_H_INCLUDED
