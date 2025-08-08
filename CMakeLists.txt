cmake_minimum_required(VERSION 3.28)
project(hft)

set(CMAKE_CXX_STANDARD 20)

add_compile_options(-Wall -Wextra -Werror -Wno-pedantic)
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-march=native)
endif ()

include_directories(
        hft
)
add_subdirectory(hft)
add_executable(HFT main.cpp)
target_link_libraries(HFT PRIVATE HftLib)

add_subdirectory(repository)
add_subdirectory(test)

set(RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
file(COPY ${RESOURCES_DIR} DESTINATION ${CMAKE_BINARY_DIR})