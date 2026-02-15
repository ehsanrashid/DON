/*
  Simple test to provoke thread creation failure. Works in two modes:
  - pthread wrapper: NativeThread::joinable() will be false if pthread_create failed.
  - std::thread: constructing too many threads throws std::system_error.

  This test is best executed inside a constrained environment (CI job with ulimit -u low,
  or container with small thread limits) to reliably trigger failure.

Steps:

mkdir -p build

g++ -std=c++17 -O2 -I./src \
    tests/thread_creation.cpp \
    $(ls src/*.cpp | grep -v 'src/main.cpp') \
    src/nnue/*.cpp \
    src/nnue/features/*.cpp \
    src/syzygy/*.cpp \
    -o build/thread_creation \
    -lpthread
g++ -std=c++17 -O2 -I./src \
    tests/thread_creation.cpp \
    $(for f in src/*.cpp; do [[ "$f" != "src/main.cpp" ]] && echo "$f"; done) \
    src/nnue/*.cpp \
    src/nnue/features/*.cpp \
    src/syzygy/*.cpp \
    -o build/thread_creation \
    -lpthread
g++ -std=c++17 -O2 -I./src `
    tests/thread_creation.cpp `
    $(Get-ChildItem src/*.cpp | Where-Object { $_.Name -ne "main.cpp" } | ForEach-Object { $_.FullName }) `
    src/nnue/*.cpp `
    src/nnue/features/*.cpp `
    src/syzygy/*.cpp `
    -o build/thread_creation `
    -lpthread

./build/thread_creation
*/

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "thread.h"

using namespace DON;

int main() {

    std::vector<std::unique_ptr<NativeThread>> threads;

    constexpr std::size_t MaxAttempt = 10000;  // upper bound

    bool failureObserved = false;

    for (std::size_t i = 0; i < MaxAttempt; ++i)
    {
        try
        {
            // Create threads that sleep briefly so they stay alive for the test
            auto th = std::make_unique<NativeThread>(
              []() { std::this_thread::sleep_for(std::chrono::milliseconds(50)); });

            // For pthread wrapper, joinable() returns false when creation failed.
            if (!th->joinable())
            {
                std::cout << "NativeThread creation failed at attempt " << i << std::endl;

                failureObserved = true;

                break;
            }

            threads.push_back(std::move(th));
        } catch (const std::system_error& e)
        {
            std::cout << "std::thread creation threw system_error at attempt " << i << " : "
                      << e.what() << std::endl;

            failureObserved = true;

            break;
        } catch (...)
        {
            std::cout << "Thread creation failed with unknown exception at attempt " << i
                      << std::endl;

            failureObserved = true;

            break;
        }
    }

    // Join created threads
    for (auto& th : threads)
        if (th && th->joinable())
            th->join();

    if (!failureObserved)
    {
        std::cerr
          << "No thread-creation failure observed. Run this test in constrained environment to provoke failure.\n";
        // Return non-zero so CI can detect "no failure observed" if that is the expectation.
        return 2;
    }

    std::cout << "thread_creation: observed failure (expected under constrained env)\n";
    return 0;
}
