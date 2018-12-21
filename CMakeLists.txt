
cmake_minimum_required(VERSION 3.4)

project(futures
    DESCRIPTION "An implementation of C++11 futures with support for continuations"
    LANGUAGES   CXX
    VERSION     0.0.1
    )

add_library(futures STATIC
    include/future.h
    )

target_compile_features(futures
    PUBLIC
        cxx_std_11
    )

target_include_directories(futures
    PUBLIC
        $<INSTALL_INTERFACE:include/>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    )

set_target_properties(futures PROPERTIES
    PUBLIC_HEADER "include/futures.h"
    LINKER_LANGUAGE CXX
    )

add_subdirectory(test)
add_subdirectory(examples)

