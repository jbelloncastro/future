
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


static void BM_ChainUnsolvedPromise(benchmark::State& state) {
    for (auto _ : state) {
        promise<int> p;
        future<int>  f = p.get_future();
        for( unsigned i = 0; i < state.range(0); i++ ) {
            f = f.then([](int i) -> int {
                    return i;
                });
        }
        p.set_value(55);
        if( f.get() != 55 )
            throw std::runtime_error("Retrieved incorrect value");
    }
}
BENCHMARK(BM_ChainUnsolvedPromise)
    ->Args({10})
    ->Args({1000})
    ->Args({1000000})
    ->Args({1000000000});

BENCHMARK_MAIN();

