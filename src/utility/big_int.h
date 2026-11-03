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

#ifndef UTILITY_BIG_INT_H_INCLUDED
#define UTILITY_BIG_INT_H_INCLUDED

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "../utility/types.h"

namespace DON {

class BigInt final {
   public:
    using Limb  = u32;
    using Limbs = std::vector<Limb>;

    static constexpr Limb Base       = 1'000'000'000;
    static constexpr u32  BaseDigits = 9;

   private:
    Limbs limbs{0};
    bool  negative = false;

    // Compare absolute values.
    static int compare_abs(const BigInt& a, const BigInt& b) noexcept {
        if (a.limbs.size() != b.limbs.size())
            return a.limbs.size() < b.limbs.size() ? -1 : 1;

        for (usize i = a.limbs.size(); i-- > 0;)
        {
            if (a.limbs[i] != b.limbs[i])
                return a.limbs[i] < b.limbs[i] ? -1 : 1;
        }

        return 0;
    }

    // Add absolute values.
    static BigInt add_abs(const BigInt& a, const BigInt& b) {
        BigInt result;
        result.limbs.resize(std::max(a.limbs.size(), b.limbs.size()) + 1);

        u64 carry = 0;

        for (usize i = 0; i + 1 < result.limbs.size(); ++i)
        {
            u64 sum = carry;

            if (i < a.limbs.size())
                sum += a.limbs[i];

            if (i < b.limbs.size())
                sum += b.limbs[i];

            result.limbs[i] = static_cast<Limb>(sum % Base);
            carry           = sum / Base;
        }

        result.limbs.back() = static_cast<Limb>(carry);
        result.trim();

        return result;
    }

    // Subtract absolute values. Requires |a| >= |b|.
    static BigInt sub_abs(const BigInt& a, const BigInt& b) {
        BigInt result;
        result.limbs.resize(a.limbs.size());

        i64 borrow = 0;

        for (usize i = 0; i < a.limbs.size(); ++i)
        {
            i64 diff = static_cast<i64>(a.limbs[i]) - borrow;

            if (i < b.limbs.size())
                diff -= b.limbs[i];

            borrow = diff < 0;

            if (borrow)
                diff += Base;

            result.limbs[i] = static_cast<Limb>(diff);
        }

        result.trim();

        return result;
    }

    // Multiply by a single limb.
    BigInt mul_small(Limb value) const {
        BigInt result;

        if (value == 0 || is_zero())
            return result;

        result.limbs.resize(limbs.size() + 1);

        u64 carry = 0;

        for (usize i = 0; i < limbs.size(); ++i)
        {
            const u64 product = static_cast<u64>(limbs[i]) * value + carry;

            result.limbs[i] = static_cast<Limb>(product % Base);
            carry           = product / Base;
        }

        result.limbs.back() = static_cast<Limb>(carry);
        result.trim();

        return result;
    }

    // Divide by a single limb. Returns the remainder.
    Limb div_small(Limb value) {
        u64 remainder = 0;

        for (usize i = limbs.size(); i-- > 0;)
        {
            const u64 current = remainder * Base + limbs[i];

            limbs[i]  = static_cast<Limb>(current / value);
            remainder = current % value;
        }

        trim();

        return static_cast<Limb>(remainder);
    }

    // Shift left by one base limb and add a new least-significant limb.
    void shift_base_add(Limb value) {
        limbs.insert(limbs.begin(), value);
        trim();
    }

    // Long division of absolute values.
    static std::pair<BigInt, BigInt> divmod_abs(const BigInt& dividend, const BigInt& divisor) {
        if (divisor.is_zero())
            throw std::domain_error("BigInt: division by zero");

        if (compare_abs(dividend, divisor) < 0)
            return {BigInt(0), dividend};

        BigInt quotient;
        BigInt remainder;

        quotient.limbs.resize(dividend.limbs.size());

        for (usize i = dividend.limbs.size(); i-- > 0;)
        {
            remainder.shift_base_add(dividend.limbs[i]);

            // Find the largest quotient limb satisfying:
            // divisor * q <= remainder.
            Limb lo = 0;
            Limb hi = Base - 1;
            Limb q  = 0;

            while (lo <= hi)
            {
                const Limb   mid     = lo + (hi - lo) / 2;
                const BigInt product = divisor.mul_small(mid);

                if (compare_abs(product, remainder) <= 0)
                {
                    q  = mid;
                    lo = mid + 1;
                }
                else
                {
                    if (mid == 0)
                        break;

                    hi = mid - 1;
                }
            }

            quotient.limbs[i] = q;

            if (q != 0)
                remainder = sub_abs(remainder, divisor.mul_small(q));
        }

        quotient.trim();
        remainder.trim();

        return {quotient, remainder};
    }

    // Parse a decimal string.
    void read(std::string_view str) {
        if (str.empty())
            throw std::invalid_argument("BigInt: empty string");

        usize pos = 0;

        if (str[pos] == '-' || str[pos] == '+')
        {
            negative = str[pos] == '-';
            ++pos;
        }

        if (pos == str.size())
            throw std::invalid_argument("BigInt: missing digits");

        for (usize i = pos; i < str.size(); ++i)
        {
            if (str[i] < '0' || str[i] > '9')
                throw std::invalid_argument("BigInt: invalid digit");
        }

        limbs.clear();

        for (usize end = str.size(); end > pos;)
        {
            const usize begin = end - pos > BaseDigits ? end - BaseDigits : pos;

            Limb value = 0;

            for (usize i = begin; i < end; ++i)
                value = value * 10 + (str[i] - '0');

            limbs.push_back(value);
            end = begin;
        }

        trim();
    }

   public:
    // Constructors

    BigInt() noexcept = default;

    BigInt(std::string_view str) noexcept { read(str); }

    template<typename T,
             typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
    BigInt(T value) noexcept {
        using U = std::make_unsigned_t<T>;

        negative = std::is_signed_v<T> && value < 0;

        U magnitude = static_cast<U>(value);

        if (negative)
            magnitude = U{0} - magnitude;

        limbs.clear();

        do
        {
            limbs.push_back(static_cast<Limb>(magnitude % Base));
            magnitude /= Base;
        } while (magnitude != 0);

        trim();
    }

    // Basic properties

    bool is_negative() const noexcept { return negative; }

    // Remove redundant most-significant zero limbs.
    void trim() noexcept {
        while (limbs.size() > 1 && limbs.back() == 0)
            limbs.pop_back();

        if (limbs.size() == 1 && limbs[0] == 0)
            negative = false;
    }

    bool is_zero() const noexcept { return limbs.size() == 1 && limbs[0] == 0; }

    usize size() const noexcept { return limbs.size(); }

    // Absolute value.

    BigInt abs() const {
        BigInt result   = *this;
        result.negative = false;
        return result;
    }

    // Unary operators

    BigInt operator+() const noexcept { return *this; }

    BigInt operator-() const noexcept {
        BigInt result = *this;

        if (!result.is_zero())
            result.negative = !result.negative;

        return result;
    }

    // Addition

    friend BigInt operator+(const BigInt& a, const BigInt& b) noexcept {
        if (a.negative == b.negative)
        {
            BigInt result   = add_abs(a, b);
            result.negative = a.negative;
            return result;
        }

        const int cmp = compare_abs(a, b);

        if (cmp == 0)
            return BigInt(0);

        BigInt result = cmp > 0 ? sub_abs(a, b) : sub_abs(b, a);

        result.negative = cmp > 0 ? a.negative : b.negative;

        return result;
    }

    // Subtraction

    friend BigInt operator-(const BigInt& a, const BigInt& b) noexcept { return a + (-b); }

    // Multiplication

    friend BigInt operator*(const BigInt& a, const BigInt& b) noexcept {
        if (a.is_zero() || b.is_zero())
            return BigInt(0);

        BigInt result;
        result.limbs.assign(a.limbs.size() + b.limbs.size() + 1, 0);

        for (usize i = 0; i < a.limbs.size(); ++i)
        {
            u64 carry = 0;

            for (usize j = 0; j < b.limbs.size(); ++j)
            {
                const u64 current =
                  result.limbs[i + j] + static_cast<u64>(a.limbs[i]) * b.limbs[j] + carry;

                result.limbs[i + j] = static_cast<Limb>(current % Base);

                carry = current / Base;
            }

            usize k = i + b.limbs.size();

            while (carry != 0)
            {
                const u64 current = result.limbs[k] + carry;

                result.limbs[k] = static_cast<Limb>(current % Base);

                carry = current / Base;
                ++k;
            }
        }

        result.negative = a.negative != b.negative;
        result.trim();

        return result;
    }

    // Division and remainder.
    // Quotient truncates toward zero.
    // Remainder has the sign of the dividend.

    friend BigInt operator/(const BigInt& a, const BigInt& b) noexcept {
        auto [quotient, remainder] = divmod_abs(a.abs(), b.abs());

        quotient.negative = (a.negative != b.negative) && !quotient.is_zero();

        return quotient;
    }

    friend BigInt operator%(const BigInt& a, const BigInt& b) noexcept {
        auto [quotient, remainder] = divmod_abs(a.abs(), b.abs());

        remainder.negative = a.negative && !remainder.is_zero();

        return remainder;
    }

    // Compound assignments

    BigInt& operator+=(const BigInt& rhs) noexcept {
        *this = *this + rhs;
        return *this;
    }

    BigInt& operator-=(const BigInt& rhs) noexcept {
        *this = *this - rhs;
        return *this;
    }

    BigInt& operator*=(const BigInt& rhs) noexcept {
        *this = *this * rhs;
        return *this;
    }

    BigInt& operator/=(const BigInt& rhs) noexcept {
        *this = *this / rhs;
        return *this;
    }

    BigInt& operator%=(const BigInt& rhs) noexcept {
        *this = *this % rhs;
        return *this;
    }

    // Comparisons

    friend bool operator==(const BigInt& a, const BigInt& b) noexcept {
        return a.negative == b.negative && a.limbs == b.limbs;
    }

    friend bool operator!=(const BigInt& a, const BigInt& b) noexcept { return !(a == b); }

    friend bool operator<(const BigInt& a, const BigInt& b) noexcept {
        if (a.negative != b.negative)
            return a.negative;

        const int cmp = compare_abs(a, b);

        return a.negative ? cmp > 0 : cmp < 0;
    }

    friend bool operator>(const BigInt& a, const BigInt& b) noexcept { return b < a; }

    friend bool operator<=(const BigInt& a, const BigInt& b) noexcept { return !(b < a); }

    friend bool operator>=(const BigInt& a, const BigInt& b) noexcept { return !(a < b); }

    // Increment and decrement

    BigInt& operator++() noexcept { return *this += 1; }

    BigInt operator++(int) noexcept {
        BigInt previous = *this;
        ++(*this);
        return previous;
    }

    BigInt& operator--() noexcept { return *this -= 1; }

    BigInt operator--(int) noexcept {
        BigInt previous = *this;
        --(*this);
        return previous;
    }

    // Checked conversion to int64_t

    explicit operator i64() const {
        const u64 limit = negative ? u64(std::numeric_limits<i64>::max()) + 1
                                   : u64(std::numeric_limits<i64>::max());

        u64 value = 0;

        for (usize i = limbs.size(); i-- > 0;)
        {
            if (value > (limit - limbs[i]) / Base)
                throw std::overflow_error("BigInt: int64_t overflow");

            value = value * Base + limbs[i];
        }

        if (!negative)
            return static_cast<i64>(value);

        if (value == limit)
            return std::numeric_limits<i64>::min();

        return -static_cast<i64>(value);
    }

    // String conversion

    std::string to_string() const noexcept {
        std::string str;

        if (negative)
            str.push_back('-');

        str += std::to_string(limbs.back());

        for (usize i = limbs.size() - 1; i-- > 0;)
        {
            const std::string part = std::to_string(limbs[i]);
            str.append(BaseDigits - part.size(), '0');
            str += part;
        }

        return str;
    }

    std::string to_hex_string() const {
        static constexpr Array<char, 16> Digits{'0', '1', '2', '3', '4', '5', '6', '7',
                                                '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

        if (is_zero())
            return "0x0";

        std::string str;

        BigInt value = abs();

        while (!value.is_zero())
            str.push_back(Digits[value.div_small(16)]);

        str += "x0";

        if (negative)
            str.push_back('-');

        std::reverse(str.begin(), str.end());

        return str;
    }

    // Stream operators

    friend std::ostream& operator<<(std::ostream& os, const BigInt& value) noexcept {
        return os << value.to_string();
    }

    friend std::istream& operator>>(std::istream& is, BigInt& value) noexcept {
        std::string str;

        if (!(is >> str))
            return is;

        try
        {
            BigInt temp(str);
            value = std::move(temp);
        } catch (const std::invalid_argument&)
        { is.setstate(std::ios::failbit); }

        return is;
    }
};


// Greatest common divisor
inline BigInt gcd(BigInt a, BigInt b) {
    a = a.abs();
    b = b.abs();

    while (!b.is_zero())
    {
        BigInt r = a % b;
        a        = std::move(b);
        b        = std::move(r);
    }

    return a;
}

// Least common multiple
inline BigInt lcm(const BigInt& a, const BigInt& b) noexcept {
    if (a.is_zero() || b.is_zero())
        return BigInt(0);

    return (a.abs() / gcd(a, b)) * b.abs();
}

// Integer exponentiation
inline BigInt pow(BigInt base, u64 exponent) noexcept {
    BigInt result = 1;

    while (exponent != 0)
    {
        if (exponent & 1)
            result *= base;

        exponent >>= 1;

        if (exponent != 0)
            base *= base;
    }

    return result;
}

// Modular exponentiation.
// Requires exponent >= 0 and modulus != 0.
inline BigInt pow_mod(BigInt base, BigInt exponent, const BigInt& modulus) {
    if (exponent.is_negative())
        throw std::domain_error("BigInt: negative exponent");

    if (modulus.is_zero())
        throw std::domain_error("BigInt: zero modulus");

    const BigInt mod = modulus.abs();

    BigInt result = BigInt(1) % mod;

    base %= mod;

    if (base.is_negative())
        base += mod;

    while (!exponent.is_zero())
    {
        if (exponent % 2 != 0)
            result = (result * base) % mod;

        exponent /= 2;

        if (!exponent.is_zero())
            base = (base * base) % mod;
    }

    return result;
}

// Factorial

inline BigInt factorial(u32 n) noexcept {
    BigInt result = 1;

    for (u32 i = 2; i <= n; ++i)
        result *= i;

    return result;
}

}  // namespace DON

#endif  // UTILITY_BIG_INT_H_INCLUDED
