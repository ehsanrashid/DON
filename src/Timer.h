#pragma once

#include <iostream>
#include <chrono>

using namespace std::chrono;

struct Timer {

    steady_clock::time_point beg;

    Timer() :
        beg{ steady_clock::now() }
    {}

    ~Timer() {
        steady_clock::time_point end{ steady_clock::now() };
        nanoseconds elapsed{ beg - end };

        auto ms = elapsed.count() * 1000.0;
        std::cout << "Timer took " << ms << " ms" << std::endl;
    }

};
