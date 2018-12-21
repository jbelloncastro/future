
cmake_minimum_required(VERSION 3.10)

find_package(GTest REQUIRED)
include(GoogleTest)

enable_testing()

add_executable(promise_construct
    promise/construct.cc
    )

target_link_libraries(promise_construct
    futures
    GTest::GTest
    GTest::Main
    )

gtest_discover_tests(promise_construct)

add_executable(future_continuation
    future/continuation.cc
    future/when_all.cc
    )

target_link_libraries(future_continuation
    futures
    GTest::GTest
    GTest::Main
    )

gtest_discover_tests(future_continuation)

add_executable(packaged_task_invocation
    packaged_task/invocation.cc
    )

target_link_libraries(packaged_task_invocation
    futures
    GTest::GTest
    GTest::Main
    )

gtest_discover_tests(packaged_task_invocation)

