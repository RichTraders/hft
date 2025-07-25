cmake_minimum_required(VERSION 3.28)
project(repository)

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-Wall -Wextra -Wpedantic -Werror)
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-march=native)
endif ()

include_directories(
        hft
)

add_executable(Repository main.cpp broker.cpp)
target_precompile_headers(Repository PUBLIC ${CMAKE_SOURCE_DIR}/include/pch.h)
target_link_libraries(Repository PRIVATE HftLib)