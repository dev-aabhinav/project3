cmake_minimum_required(VERSION 3.16)

project(IndexEngine LANGUAGES CXX)


# ---------------------------------------------------------------
# Language standard and default build type
# ---------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)


# Optional AddressSanitizer + UndefinedBehaviorSanitizer build:
#   cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
option(ENABLE_SANITIZERS "Build with ASan and UBSan" OFF)

if(ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()


# ---------------------------------------------------------------
# The engine itself: one static library
# ---------------------------------------------------------------

add_library(index_engine
    src/disk_manager.cpp
)

target_include_directories(index_engine PUBLIC include)


# ---------------------------------------------------------------
# Tests (GoogleTest)
# Use the system copy if installed, otherwise download a pinned version.
# ---------------------------------------------------------------

enable_testing()

find_package(GTest QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.14.0
    )

    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

add_executable(unit_tests
    tests/disk_manager_test.cpp
)

target_link_libraries(unit_tests PRIVATE index_engine GTest::gtest_main)

gtest_discover_tests(unit_tests)
