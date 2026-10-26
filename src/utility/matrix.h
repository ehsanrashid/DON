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

#ifndef UTILITY_MATRIX_H_INCLUDED
#define UTILITY_MATRIX_H_INCLUDED

#include <array>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

#include "utility/types.h"

namespace DON {

template<usize M, usize N, typename T = double>
class Matrix final {
    static_assert(M > 0 && N > 0, "Matrix dimensions must be positive");

   public:
    using value_type = T;

    static constexpr usize Rows = M;
    static constexpr usize Cols = N;
    static constexpr usize Size = M * N;

    Array<T, M, N> data{};

    constexpr Matrix() noexcept = default;

    // Initialize all elements to the same value.
    constexpr explicit Matrix(const T& value) noexcept {
        for (usize i = 0; i < M; ++i)
            data[i].fill(value);
    }

    // Initialize elements in i-major order.
    //template<typename... Args, typename = std::enable_if_t<sizeof...(Args) == Size>>
    //constexpr Matrix(Args&&... args) noexcept {
    //    usize index = 0;
    //    ((data[index / N][index % N] = static_cast<T>(std::forward<Args>(args)), ++index), ...);
    //}
    template<typename... Args,
             std::enable_if_t<sizeof...(Args) == Size && (std::is_convertible_v<Args&&, T> && ...),
                              int> = 0>
    constexpr Matrix(Args&&... args) noexcept {
        usize index = 0;

        ((data[index / N][index % N] = static_cast<T>(std::forward<Args>(args)), ++index), ...);
    }

    Matrix(std::initializer_list<std::initializer_list<T>> matrix) {
        if (matrix.size() != M)
            throw std::invalid_argument("Matrix: incorrect number of rows");

        usize i = 0;

        for (const auto& row : matrix)
        {
            if (row.size() != N)
                throw std::invalid_argument("Matrix: incorrect number of columns");

            usize j = 0;

            for (const T& value : row)
            {
                data[i][j] = value;
                ++j;
            }

            ++i;
        }
    }

    // Element access: unchecked.
    [[nodiscard]] constexpr T& operator()(usize i, usize j) noexcept { return data[i][j]; }

    [[nodiscard]] constexpr const T& operator()(usize i, usize j) const noexcept {
        return data[i][j];
    }

    // Element access: checked.
    [[nodiscard]] constexpr T& at(usize i, usize j) {
        check_index(i, j);
        return data[i][j];
    }

    [[nodiscard]] constexpr const T& at(usize i, usize j) const {
        check_index(i, j);
        return data[i][j];
    }

    // Addition.
    [[nodiscard]] constexpr Matrix operator+(const Matrix& matrix) const noexcept {
        Matrix result;

        for (usize i = 0; i < M; ++i)
            for (usize j = 0; j < N; ++j)
                result(i, j) = (*this)(i, j) + matrix(i, j);

        return result;
    }

    constexpr Matrix& operator+=(const Matrix& matrix) noexcept {
        for (usize i = 0; i < M; ++i)
            for (usize j = 0; j < N; ++j)
                (*this)(i, j) += matrix(i, j);

        return *this;
    }

    // Subtraction.
    [[nodiscard]] constexpr Matrix operator-(const Matrix& matrix) const noexcept {
        Matrix result;

        for (usize i = 0; i < M; ++i)
            for (usize j = 0; j < N; ++j)
                result(i, j) = (*this)(i, j) - matrix(i, j);

        return result;
    }

    constexpr Matrix& operator-=(const Matrix& matrix) noexcept {
        for (usize i = 0; i < M; ++i)
            for (usize j = 0; j < N; ++j)
                (*this)(i, j) -= matrix(i, j);

        return *this;
    }

    // Unary operators.
    [[nodiscard]] constexpr Matrix operator+() const noexcept { return *this; }

    [[nodiscard]] constexpr Matrix operator-() const noexcept {
        Matrix result;

        for (usize i = 0; i < M; ++i)
            for (usize j = 0; j < N; ++j)
                result(i, j) = -(*this)(i, j);

        return result;
    }

    // Matrix multiplication.
    template<usize P>
    [[nodiscard]] constexpr Matrix<M, P, T>
    operator*(const Matrix<N, P, T>& matrix) const noexcept {
        Matrix<M, P, T> result{};

        for (usize i = 0; i < M; ++i)
            for (usize j = 0; j < P; ++j)
                for (usize k = 0; k < N; ++k)
                    result(i, j) += (*this)(i, k) * matrix(k, j);

        return result;
    }

    // Scalar multiplication.
    template<typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    [[nodiscard]] constexpr auto operator*(U scalar) const noexcept
      -> Matrix<M, N, std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;

        Matrix<M, N, R> result{};

        for (usize i = 0; i < M; ++i)
            for (usize j = 0; j < N; ++j)
                result(i, j) = static_cast<R>((*this)(i, j)) * scalar;

        return result;
    }

    template<typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    friend constexpr auto operator*(U scalar, const Matrix& matrix) noexcept
      -> Matrix<M, N, std::common_type_t<T, U>> {
        return matrix * scalar;
    }

    // Scalar division.
    template<typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    [[nodiscard]] auto operator/(U scalar) const -> Matrix<M, N, std::common_type_t<T, U>> {
        if (scalar == U{})
            throw std::domain_error("Matrix division by zero");

        using R = std::common_type_t<T, U>;

        Matrix<M, N, R> result{};

        for (usize i = 0; i < M; ++i)
            for (usize j = 0; j < N; ++j)
                result(i, j) = static_cast<R>((*this)(i, j)) / scalar;

        return result;
    }

    // Element-wise division. This is not standard matrix division.
    [[nodiscard]] Matrix operator/(const Matrix& matrix) const {
        Matrix result;

        for (usize i = 0; i < M; ++i)
        {
            for (usize j = 0; j < N; ++j)
            {
                if (matrix(i, j) == T{})
                    throw std::domain_error("Matrix element-wise division by zero");

                result(i, j) = (*this)(i, j) / matrix(i, j);
            }
        }

        return result;
    }

    // Compound scalar multiplication.
    constexpr Matrix& operator*=(const T& scalar) noexcept {
        for (usize i = 0; i < M; ++i)
            for (usize j = 0; j < N; ++j)
                (*this)(i, j) *= scalar;

        return *this;
    }

    // Compound scalar division.
    Matrix& operator/=(const T& scalar) {
        if (scalar == T{})
            throw std::domain_error("Matrix division by zero");

        for (usize i = 0; i < M; ++i)
            for (usize j = 0; j < N; ++j)
                (*this)(i, j) /= scalar;

        return *this;
    }

    // Equality.
    [[nodiscard]] constexpr bool operator==(const Matrix& matrix) const noexcept {
        for (usize i = 0; i < M; ++i)
            for (usize j = 0; j < N; ++j)
                if ((*this)(i, j) != matrix(i, j))
                    return false;

        return true;
    }

    [[nodiscard]] constexpr bool operator!=(const Matrix& matrix) const noexcept {
        return !(*this == matrix);
    }

    // Transpose: Matrix<M, N> -> Matrix<N, M>.
    [[nodiscard]] constexpr Matrix<N, M, T> transpose() const noexcept {
        Matrix<N, M, T> result{};

        for (usize i = 0; i < M; ++i)
            for (usize j = 0; j < N; ++j)
                result(j, i) = (*this)(i, j);

        return result;
    }

    // Adjoint for real-valued matrices.
    // For complex-valued elements, conjugation is also required.
    [[nodiscard]] constexpr Matrix<N, M, T> adjoint() const noexcept { return transpose(); }

    // Identity matrix.
    [[nodiscard]] static constexpr Matrix identity() noexcept {
        static_assert(M == N, "Identity matrix must be square");

        Matrix result{};

        for (usize i = 0; i < M; ++i)
            result(i, i) = T{1};

        return result;
    }

    // Submatrix: remove the specified i and column.
    [[nodiscard]] auto submatrix(usize i, usize j) const {
        static_assert(M == N, "Submatrix requires a square matrix");
        static_assert(M > 1, "Cannot create a zero-sized submatrix");

        check_index(i, j);

        Matrix<M - 1, N - 1, T> result{};

        usize subRow = 0;

        for (usize r = 0; r < M; ++r)
        {
            if (r == i)
                continue;

            usize subCol = 0;

            for (usize c = 0; c < N; ++c)
            {
                if (c == j)
                    continue;

                result(subRow, subCol++) = (*this)(r, c);
            }

            ++subRow;
        }

        return result;
    }

    // Minor: determinant of the submatrix.
    [[nodiscard]] T minor(usize i, usize j) const {
        static_assert(M == N, "Minor requires a square matrix");

        check_index(i, j);

        if constexpr (M == 1)
        {
            return T{1};
        }
        else
        {
            return submatrix(i, j).determinant();
        }
    }

    // Cofactor: C(i, j) = (-1)^(i+j) * minor(i, j).
    [[nodiscard]] T cofactor(usize i, usize j) const {
        return ((i + j) % 2 == 0 ? T{1} : T{-1}) * minor(i, j);
    }

    // Determinant: recursive Laplace expansion along the first i.
    [[nodiscard]] T determinant() const {
        static_assert(M == N, "Determinant requires a square matrix");

        if constexpr (M == 1)
        {
            return data[0][0];
        }
        else
        {
            T result{};

            for (usize j = 0; j < N; ++j)
                result += (*this)(0, j) * cofactor(0, j);

            return result;
        }
    }

    // String conversion.
    [[nodiscard]] std::string to_string() const {
        std::ostringstream oss;

        //
        // Matrix [[]
        //         []]

        oss << "[";

        for (usize i = 0; i < M; ++i)
        {
            if (i != 0)
                oss << "\n ";

            oss << "[ ";

            for (usize j = 0; j < N; ++j)
            {
                if (j != 0)
                    oss << ", ";

                oss << (*this)(i, j);
            }

            oss << " ]";
        }

        oss << "]";

        //
        // Matrix []
        //        []

        /*
        for (usize i = 0; i < M; ++i)
        {
            if (i != 0)
                oss << "\n";

            oss << "[ ";

            for (usize j = 0; j < N; ++j)
            {
                if (j != 0)
                    oss << ", ";

                oss << (*this)(i, j);
            }

            oss << " ]";
        }
        */

        return oss.str();
    }

    // Scalar addition.
    template<typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    [[nodiscard]] constexpr auto operator+(U scalar) const noexcept
      -> Matrix<M, N, std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;

        Matrix<M, N, R> result{};

        for (usize i = 0; i < M; ++i)
            for (usize j = 0; j < N; ++j)
                result(i, j) = static_cast<R>((*this)(i, j)) + scalar;

        return result;
    }

    // Scalar subtraction.
    template<typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    [[nodiscard]] constexpr auto operator-(U scalar) const noexcept
      -> Matrix<M, N, std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;

        Matrix<M, N, R> result{};

        for (usize i = 0; i < M; ++i)
            for (usize j = 0; j < N; ++j)
                result(i, j) = static_cast<R>((*this)(i, j)) - scalar;

        return result;
    }

    // Scalar-left addition.
    template<typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    friend constexpr auto operator+(U scalar, const Matrix& matrix) noexcept
      -> Matrix<M, N, std::common_type_t<T, U>> {
        return matrix + scalar;
    }

    // Scalar-left subtraction.
    template<typename U, typename = std::enable_if_t<std::is_arithmetic_v<U>>>
    friend constexpr auto operator-(U scalar, const Matrix& matrix) noexcept
      -> Matrix<M, N, std::common_type_t<T, U>> {
        using R = std::common_type_t<T, U>;

        Matrix<M, N, R> result{};

        for (usize i = 0; i < M; ++i)
            for (usize j = 0; j < N; ++j)
                result(i, j) = static_cast<R>(scalar) - static_cast<R>(matrix(i, j));

        return result;
    }

    // Stream output.
    friend std::ostream& operator<<(std::ostream& os, const Matrix& matrix) {
        return os << matrix.to_string();
    }

   private:
    static void check_index(usize i, usize j) {
        if (i >= M || j >= N)
            throw std::out_of_range("Matrix index out of range");
    }
};

}  // namespace DON

#endif  // UTILITY_MATRIX_H_INCLUDED
