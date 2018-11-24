
enable_testing()
find_package(GTest REQUIRED)
include(GoogleTest)

add_executable(test
    test.cc
    )

target_link_libraries(test
    futures
    GTest::GTest
    GTest::Main
    )

gtest_discover_tests(test)

