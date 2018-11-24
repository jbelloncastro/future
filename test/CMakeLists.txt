
cmake_minimum_required(VERSION 3.10)

find_package(GTest REQUIRED)
include(GoogleTest)

enable_testing()

add_executable(test
    test.cc
    )

target_link_libraries(test
    futures
    GTest::GTest
    GTest::Main
    )

gtest_discover_tests(test)

