/*

cmake -S tests -B tests/build `
    -DCMAKE_PREFIX_PATH="C:/Libraries/benchmark" `
    -DCMAKE_BUILD_TYPE=Release

cmake --build tests/build --config Release -j8

.\tests\build\Release\prng_benchmark.exe

.\tests\build\Release\prng_benchmark.exe `
    --benchmark_min_time=3s `
    --benchmark_repetitions=5 `
    --benchmark_report_aggregates_only=true

*/


#include <benchmark/benchmark.h>

#include "utility/prng.h"
#include "utility/types.h"

// --- Template Benchmark Harness ---
template<typename Engine>
static void BM_PrngSpeed(benchmark::State& state) noexcept {
    Engine prng{0};

    for (auto _ : state)
    {
        auto value = prng.template rand<DON::u64>();
        benchmark::DoNotOptimize(value);
    }

    state.SetItemsProcessed(state.iterations());
}

// --- Register the Benchmarks ---
BENCHMARK_TEMPLATE(BM_PrngSpeed, DON::Xorshift64s);
BENCHMARK_TEMPLATE(BM_PrngSpeed, DON::Xoroshiro128ss);
BENCHMARK_TEMPLATE(BM_PrngSpeed, DON::Xoshiro256ss);
BENCHMARK_TEMPLATE(BM_PrngSpeed, DON::Xoshiro512ss);
BENCHMARK_TEMPLATE(BM_PrngSpeed, DON::Xoshiro1024ss);

BENCHMARK_MAIN();
