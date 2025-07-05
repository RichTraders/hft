cmake_minimum_required(VERSION 3.30)
project(hft)

set(CMAKE_CXX_STANDARD 20)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-march=native)
endif ()

add_executable(hft main.cpp)
