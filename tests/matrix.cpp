
/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -I./src tests/matrix.cpp -o build/matrix
g++ -std=c++17 -O2 -DNDEBUG -I./src tests/matrix.cpp -o build/matrix

./build/matrix
*/

#include <iostream>

#include "utility/matrix.h"

using namespace DON;

int main() {
    using DON::Matrix;

    Matrix<2, 2, double> a;
    a(0, 0) = 1.0;
    a(0, 1) = 2.0;
    a(1, 0) = 3.0;
    a(1, 1) = 4.0;

    Matrix<2, 2, double> b;
    b(0, 0) = 5.0;
    b(0, 1) = 6.0;
    b(1, 0) = 7.0;
    b(1, 1) = 8.0;

    std::cout << "Matrix A:\n" << a << "\n\n";
    std::cout << "Matrix B:\n" << b << "\n\n";

    std::cout << "A + B:\n" << a + b << "\n\n";
    std::cout << "A - B:\n" << a - b << "\n\n";
    std::cout << "A * B:\n" << a * b << "\n\n";

    std::cout << "A * 2:\n" << a * 2.0 << "\n\n";
    std::cout << "A / 2:\n" << a / 2.0 << "\n\n";

    std::cout << "Transpose A:\n" << a.transpose() << "\n\n";

    std::cout << "Minor(0, 0): " << a.minor(0, 0) << '\n';
    std::cout << "Cofactor(0, 0): " << a.cofactor(0, 0) << '\n';
    std::cout << "Determinant A: " << a.determinant() << '\n';

    std::cout << "\nIdentity matrix:\n" << Matrix<2, 2, double>::identity() << '\n';

    const Matrix<3, 3, double> c{1.0, 2.0, 3.0, 0.0, 4.0, 5.0, 1.0, 0.0, 6.0};

    std::cout << "\nMatrix C:\n" << c << '\n';

    std::cout << "Minor(0, 0): " << c.minor(0, 0) << '\n';
    std::cout << "Cofactor(0, 0): " << c.cofactor(0, 0) << '\n';
    std::cout << "Determinant C: " << c.determinant() << '\n';

    std::cout << "\nSubmatrix(0, 0):\n" << c.submatrix(0, 0) << '\n';

    std::cout << "\nIdentity matrix:\n" << Matrix<3, 3, double>::identity() << '\n';

    const Matrix<3, 2, double> d{
        1.0, 2.0,
        3.0, 4.0,
        5.0, 6.0
    };

    std::cout << "\nMatrix D (3x2):\n" << d << '\n';
    std::cout << "\nTranspose D (2x3):\n" << d.transpose() << '\n';

    return 0;
}
