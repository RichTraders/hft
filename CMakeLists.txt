cmake_minimum_required(VERSION 3.28)
project(hft)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Werror -Wno-pedantic)
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-march=native)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS_RELEASE   "${CMAKE_C_FLAGS_RELEASE} -g -fno-omit-frame-pointer")
    # Enable LTO for maximum performance
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif ()

include_directories(
        hft
)
add_subdirectory(hft)

option(ENABLE_TESTS "Build and fetch test dependencies" OFF)

set(STRATEGY_CONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/strategy_config.hpp.in")

function(add_strategy_target target_name strategy_type strategy_name)
    set(gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${target_name}")
    file(MAKE_DIRECTORY "${gen_dir}")
    set(STRATEGY_TYPE "${strategy_type}")
    set(STRATEGY_NAME "${strategy_name}")
    configure_file("${STRATEGY_CONFIG_TEMPLATE}" "${gen_dir}/strategy_config.hpp" @ONLY)

    add_executable(${target_name} main.cpp)
    target_include_directories(${target_name} PRIVATE "${gen_dir}")
    target_link_libraries(${target_name} PRIVATE HftLib)

    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif ()
endfunction()

add_strategy_target(HFT_maker "trading::MarketMaker" "MarketMaker")
add_strategy_target(HFT_taker "trading::LiquidTaker" "LiquidTaker")
# Default target for backward compatibility (uses market maker strategy)
add_strategy_target(HFT "trading::MarketMaker" "MarketMaker")

add_subdirectory(repository)
if (ENABLE_TESTS)
    add_subdirectory(test)
endif ()

set(RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
file(COPY ${RESOURCES_DIR} DESTINATION ${CMAKE_BINARY_DIR})
file(COPY util/set_cpu.sh DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${RESOURCES_DIR} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
