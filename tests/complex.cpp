/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -I./src tests/complex.cpp -o build/complex
g++ -std=c++17 -O2 -DNDEBUG -I./src tests/complex.cpp -o build/complex

./build/complex
*/

#include <iostream>

#include "utility/complex.h"

using namespace DON;

#include <iostream>

int main() {
    using DON::Complex;

    Complex<double> a{3.0, 4.0};
    Complex<float>  b{1.0F, 2.0F};

    std::cout << std::boolalpha;

    std::cout << "a = " << a.to_string() << '\n';
    std::cout << "b = " << b.to_string() << "\n\n";

    // Complex arithmetic
    std::cout << "--- Complex Arithmetic ---\n";

    auto sum      = a + b;
    auto diff     = a - b;
    auto product  = a * b;
    auto quotient = a / b;

    std::cout << "a + b = " << sum.to_string() << '\n';
    std::cout << "a - b = " << diff.to_string() << '\n';
    std::cout << "a * b = " << product.to_string() << '\n';
    std::cout << "a / b = " << quotient.to_string() << "\n\n";

    // Scalar arithmetic
    std::cout << "--- Scalar Arithmetic ---\n";

    auto c = a + 2.0;
    auto d = a * 2;
    auto e = 2.0 + a;
    auto f = 2.0 - a;
    auto g = 2.0 / a;

    std::cout << "a + 2.0 = " << c.to_string() << '\n';
    std::cout << "a * 2   = " << d.to_string() << '\n';
    std::cout << "2.0 + a = " << e.to_string() << '\n';
    std::cout << "2.0 - a = " << f.to_string() << '\n';
    std::cout << "2.0 / a = " << g.to_string() << "\n\n";

    // Complex properties
    std::cout << "--- Complex Properties ---\n";

    auto conjugate = a.conjugate();
    auto adjoint   = a.adjoint();
    auto norm      = a.norm();
    auto magnitude = a.magnitude();

    std::cout << "conjugate(a) = " << conjugate.to_string() << '\n';
    std::cout << "adjoint(a)   = " << adjoint.to_string() << '\n';
    std::cout << "norm(a)      = " << norm << '\n';
    std::cout << "magnitude(a) = " << magnitude << "\n\n";

    // String conversion
    std::cout << "--- String Conversion ---\n";

    auto text = a.to_string();

    std::cout << "a.to_string() = \"" << text << "\"\n\n";

    // Equality
    std::cout << "--- Equality ---\n";

    std::cout << "a == b = " << (a == b) << '\n';
    std::cout << "a != b = " << (a != b) << "\n\n";

    // Unary operators
    std::cout << "--- Unary Operators ---\n";

    std::cout << "+a = " << (+a).to_string() << '\n';
    std::cout << "-a = " << (-a).to_string() << "\n\n";

    // Compound assignments
    std::cout << "--- Compound Assignments ---\n";

    a += Complex<double>{1.0, 1.0};
    std::cout << "a += (1 + i): " << a.to_string() << '\n';

    a -= Complex<double>{1.0, 1.0};
    std::cout << "a -= (1 + i): " << a.to_string() << '\n';

    a *= Complex<double>{2.0, 0.0};
    std::cout << "a *= 2:       " << a.to_string() << '\n';

    a /= Complex<double>{2.0, 0.0};
    std::cout << "a /= 2:       " << a.to_string() << '\n';

    // Division by zero
    std::cout << "\n--- Division by Zero ---\n";

    try {
        const Complex<double> zero{0.0, 0.0};
        auto result = a / zero;

        std::cout << result.to_string() << '\n';
    } catch (const std::domain_error& error) {
        std::cout << "Exception: " << error.what() << '\n';
    }

    return 0;
}
