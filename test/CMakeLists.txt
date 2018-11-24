
cmake_minimum_required(VERSION 3.10)

find_package(GTest REQUIRED)
include(GoogleTest)

enable_testing()

add_executable(construction
    construction.cc
    )

target_link_libraries(construction
    futures
    GTest::GTest
    GTest::Main
    )

gtest_discover_tests(construction)

add_executable(continuation
    continuation.cc
    )

target_link_libraries(continuation
    futures
    GTest::GTest
    GTest::Main
    )

gtest_discover_tests(continuation)

