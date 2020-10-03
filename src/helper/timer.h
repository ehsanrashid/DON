#pragma once

#include <chrono>
#include <iostream>

using namespace std::chrono;

struct Timer {

    Timer() noexcept :
        start{ steady_clock::now() } {
    }

    ~Timer() noexcept {
        steady_clock::time_point stop{ steady_clock::now() };
        nanoseconds const elapsed{ stop - start };

        auto const ms = elapsed.count() * 1000.0;
        std::cout << "Time elapsed: " << ms << " ms" << std::endl;
    }

    steady_clock::time_point start;
};
