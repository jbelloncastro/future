
#include "future.h"

#include <benchmark/benchmark.h>

static void BM_DefaultPromiseCreation(benchmark::State& state) {
    for (auto _ : state) {
        promise<int> p;
    }
}
BENCHMARK(BM_DefaultPromiseCreation);

static void BM_PromiseWithFutureCreation(benchmark::State& state) {
    for (auto _ : state) {
        promise<int> p;
        future<int>  f = p.get_future();
    }
}
BENCHMARK(BM_PromiseWithFutureCreation);

static void BM_ChainSolvedPromise(benchmark::State& state) {
    promise<int> p;
    future<int>  f = p.get_future();
    p.set_value(0);
    for (auto _ : state) {
        f = f.then([](int i) -> int {
            return i;
        });
    }
}
BENCHMARK(BM_ChainSolvedPromise);

BENCHMARK_MAIN();

