
cmake_minimum_required(VERSION 3.10)

add_executable(bm_continuation
    continuation.cc
    )

target_link_libraries(bm_continuation
    futures
    benchmark
    benchmark_main
    )

