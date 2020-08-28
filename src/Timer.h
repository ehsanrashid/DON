#pragma once

#include <iostream>
#include <chrono>

struct Timer {

    std::chrono::steady_clock::time_point start, end;
    std::chrono::nanoseconds duration;

    Timer() {
        start = std::chrono::steady_clock::now();
    }

    ~Timer() {
        end = std::chrono::steady_clock::now();
        duration = start - end;
        auto ms = duration.count() * 1000.0;
        std::cout << "Timer took " << ms << " ms" << std::endl;
    }

};
