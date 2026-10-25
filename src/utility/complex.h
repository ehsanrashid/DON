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

#ifndef UTILITY_COMPLEX_H_INCLUDED
#define UTILITY_COMPLEX_H_INCLUDED

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace DON {

template<typename T>
class Complex final {
   public:
    T real{};
    T imag{};

    constexpr Complex() noexcept = default;

    constexpr Complex(const T& real, const T& imag = T{}) noexcept :
        real(real),
        imag(imag) {}

    // Addition
    template<typename U>
    [[nodiscard]] constexpr auto operator+(const Complex<U>& other) const noexcept
      -> Complex<std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;

        return {static_cast<R>(real) + static_cast<R>(other.real),
                static_cast<R>(imag) + static_cast<R>(other.imag)};
    }

    constexpr Complex& operator+=(const Complex& other) noexcept {
        real += other.real;
        imag += other.imag;
        return *this;
    }

    // Subtraction
    template<typename U>
    [[nodiscard]] constexpr auto operator-(const Complex<U>& other) const noexcept
      -> Complex<std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;

        return {static_cast<R>(real) - static_cast<R>(other.real),
                static_cast<R>(imag) - static_cast<R>(other.imag)};
    }

    constexpr Complex& operator-=(const Complex& other) noexcept {
        real -= other.real;
        imag -= other.imag;
        return *this;
    }

    // Multiplication
    template<typename U>
    [[nodiscard]] constexpr auto operator*(const Complex<U>& other) const noexcept
      -> Complex<std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;

        const R lhsReal = static_cast<R>(real);
        const R lhsImag = static_cast<R>(imag);
        const R rhsReal = static_cast<R>(other.real);
        const R rhsImag = static_cast<R>(other.imag);

        return {lhsReal * rhsReal - lhsImag * rhsImag, lhsReal * rhsImag + lhsImag * rhsReal};
    }

    constexpr Complex& operator*=(const Complex& other) noexcept {
        *this = *this * other;
        return *this;
    }

    // Division
    template<typename U>
    [[nodiscard]] constexpr auto operator/(const Complex<U>& other) const
      -> Complex<std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;

        if (other.real == U{} && other.imag == U{})
            throw std::domain_error("Complex division by zero");

        const R lhsReal = static_cast<R>(real);
        const R lhsImag = static_cast<R>(imag);
        const R rhsReal = static_cast<R>(other.real);
        const R rhsImag = static_cast<R>(other.imag);

        const R denominator = rhsReal * rhsReal + rhsImag * rhsImag;

        return {(lhsReal * rhsReal + lhsImag * rhsImag) / denominator,
                (lhsImag * rhsReal - lhsReal * rhsImag) / denominator};
    }

    constexpr Complex& operator/=(const Complex& other) {
        *this = *this / other;
        return *this;
    }

    // Scalar arithmetic: Complex op scalar
    template<typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    [[nodiscard]] constexpr auto operator+(U scalar) const noexcept
      -> Complex<std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;
        return {static_cast<R>(real) + scalar, static_cast<R>(imag)};
    }

    template<typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    [[nodiscard]] constexpr auto operator-(U scalar) const noexcept
      -> Complex<std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;
        return {static_cast<R>(real) - scalar, static_cast<R>(imag)};
    }

    template<typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    [[nodiscard]] constexpr auto operator*(U scalar) const noexcept
      -> Complex<std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;
        return {static_cast<R>(real) * scalar, static_cast<R>(imag) * scalar};
    }

    template<typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    [[nodiscard]] constexpr auto operator/(U scalar) const -> Complex<std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;

        if (scalar == U{})
            throw std::domain_error("Complex division by zero");

        return {static_cast<R>(real) / scalar, static_cast<R>(imag) / scalar};
    }

    // Scalar-left arithmetic
    template<typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    friend constexpr auto operator+(U scalar, const Complex& value) noexcept
      -> Complex<std::common_type_t<T, U>> {
        return value + scalar;
    }

    template<typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    friend constexpr auto operator-(U scalar, const Complex& value) noexcept
      -> Complex<std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;
        return {static_cast<R>(scalar) - static_cast<R>(value.real), -static_cast<R>(value.imag)};
    }

    template<typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    friend constexpr auto operator*(U scalar, const Complex& value) noexcept
      -> Complex<std::common_type_t<T, U>> {
        return value * scalar;
    }

    template<typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    friend constexpr auto operator/(U scalar, const Complex& value)
      -> Complex<std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;

        if (value.real == T{} && value.imag == T{})
            throw std::domain_error("Complex division by zero");

        const R lhs         = static_cast<R>(scalar);
        const R rhsReal     = static_cast<R>(value.real);
        const R rhsImag     = static_cast<R>(value.imag);
        const R denominator = rhsReal * rhsReal + rhsImag * rhsImag;

        return {lhs * rhsReal / denominator, -lhs * rhsImag / denominator};
    }

    // Unary operators
    [[nodiscard]] constexpr Complex operator+() const noexcept { return *this; }

    [[nodiscard]] constexpr Complex operator-() const noexcept { return {-real, -imag}; }

    // Equality
    template<typename U>
    [[nodiscard]] constexpr bool operator==(const Complex<U>& other) const noexcept {
        return real == other.real && imag == other.imag;
    }

    template<typename U>
    [[nodiscard]] constexpr bool operator!=(const Complex<U>& other) const noexcept {
        return !(*this == other);
    }

    // Complex conjugate
    [[nodiscard]] constexpr Complex conjugate() const noexcept { return {real, -imag}; }

    // Adjoint of a complex scalar equals its complex conjugate
    [[nodiscard]] constexpr Complex adjoint() const noexcept { return conjugate(); }

    // Squared magnitude: real^2 + imag^2
    [[nodiscard]] constexpr T norm() const noexcept { return real * real + imag * imag; }

    // Magnitude: sqrt(real^2 + imag^2)
    [[nodiscard]] auto magnitude() const noexcept {
        using std::sqrt;
        return sqrt(norm());
    }

    // String conversion
    [[nodiscard]] std::string to_string() const {
        std::ostringstream stream;

        stream << real;

        if (imag > T{})
        {
            stream << " + " << imag << 'i';
        }
        else if (imag < T{})
        {
            stream << " - " << -imag << 'i';
        }

        return stream.str();
    }
};

}  // namespace DON

#endif  // UTILITY_COMPLEX_H_INCLUDED
