
cmake_minimum_required(VERSION 3.16)

project(DON_PRNG_Benchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(benchmark CONFIG REQUIRED)

add_executable(prng_benchmark
    prng_benchmark.cpp
    ../src/utility/prng.cpp
)

target_include_directories(prng_benchmark PRIVATE
    ../src
)

target_link_libraries(prng_benchmark PRIVATE
    benchmark::benchmark
)
