#pragma once

#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono;

class Timer {

public:

    template<typename Function>
    void interval(int interval, Function function) {
        _stop = false;
        std::thread thread([=]() {
            while (true) {
                if (_stop) return;
                std::this_thread::sleep_for(milliseconds(interval));
                if (_stop) return;
                function();
            }
            });
        thread.detach();
    }

    template<typename Function>
    void timeout(int delay, Function function) {
        _stop = false;

        std::thread thread([=]() {
            if (_stop) return;
            std::this_thread::sleep_for(milliseconds(delay));
            if (_stop) return;
            function();
            });
        thread.detach();
    }

    void stop() noexcept {
        _stop = true;
    }

private:
    bool _stop = false;
};
/*
Timer timer;

timer.interval(1000, [&]() {
    cout << "Hey.. After each 1s..." << endl;
});

timer.timeout(5200, [&]() {
    cout << "Hey.. After 5.2s. But I will stop the timer!" << endl;
    timer.stop();
});

// 'interval' allows to run the code of the same function repeatedly, at a given interval.
// In the above example, the function is a lambda that displays “Hey.. After each 1s…”.
// And 'timeout' plans one execution of a function in a given amount of time,
// here printing “Hey.. After 5.2s. But I will stop the timer!” and stopping the timer, in 5200 milliseconds.
*/

struct TimeElapser {

    TimeElapser() noexcept :
        start{ steady_clock::now() } {
    }

    ~TimeElapser() noexcept {
        steady_clock::time_point const stop{ steady_clock::now() };
        nanoseconds const elapsed{ stop - start };

        auto const ms = elapsed.count() * 1000.0;
        std::cout << "Time elapsed: " << ms << " ms" << std::endl;
    }

    steady_clock::time_point start;
};

