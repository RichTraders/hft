cmake_minimum_required(VERSION 3.28)
project(hft)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Werror -Wno-pedantic)
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-march=native)
    # Enable LTO for maximum performance
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
elseif (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_options(-march=native)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -g -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -g -fno-omit-frame-pointer")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif ()

option(ENABLE_WEBSOCKET "Build WebSocket transport support" ON)
option(ENABLE_SBE_DECODER "Use SBE decoder for WebSocket market data" OFF)
option(ENABLE_SBE_DECODER_ORDER_ENTRY "Use SBE decoder for WebSocket order entry" OFF)
option(USE_FUTURES_API "Use Binance Futures API instead of Spot API" ON)
option(ENABLE_TESTS "Build and fetch test dependencies" ON)

if (ENABLE_WEBSOCKET)
    include(${CMAKE_SOURCE_DIR}/cmake/libwebsockets.cmake)
    include(${CMAKE_SOURCE_DIR}/cmake/glaze.cmake)
    message(STATUS "WebSocket transport enabled")
endif ()

if (ENABLE_SBE_DECODER AND NOT ENABLE_WEBSOCKET)
    message(FATAL_ERROR "ENABLE_SBE_DECODER requires ENABLE_WEBSOCKET=ON")
endif ()

if (ENABLE_SBE_DECODER)
    add_compile_definitions(ENABLE_SBE_DECODER)
    message(STATUS "SBE binary decoder enabled for WebSocket market data")
else ()
    message(STATUS "JSON decoder enabled for WebSocket market data (default)")
endif ()

if (USE_FUTURES_API)
    add_compile_definitions(USE_FUTURES_API)
    message(STATUS "Using Binance Futures API")
else ()
    message(STATUS "Using Binance Spot API (default)")
endif ()

include_directories(
        hft
)
add_subdirectory(hft)

set(STRATEGY_CONFIG_TEMPLATE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/strategy_config.hpp.in")

function(add_strategy_target target_name strategy_type strategy_name)
    set(gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated/${target_name}")
    file(MAKE_DIRECTORY "${gen_dir}")
    set(STRATEGY_TYPE "${strategy_type}")
    set(STRATEGY_NAME "${strategy_name}")
    configure_file("${STRATEGY_CONFIG_TEMPLATE}" "${gen_dir}/strategy_config.hpp" @ONLY)

    add_executable(${target_name} main.cpp)
    target_include_directories(${target_name} PRIVATE "${gen_dir}")
    if (ENABLE_WEBSOCKET)
        target_link_libraries(${target_name} PRIVATE HftLib_ws)
        target_compile_definitions(${target_name} PRIVATE ENABLE_WEBSOCKET)
    else ()
        target_link_libraries(${target_name} PRIVATE HftLib_fix)
    endif ()

    if (CMAKE_BUILD_TYPE STREQUAL "Release")
        set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif ()
endfunction()

add_strategy_target(HFT_maker "trading::ObiVwapMomentumStrategy" "ObiVwapMomentumStrategy")
add_strategy_target(HFT_taker "trading::LiquidTaker" "LiquidTaker")
# Default target for backward compatibility (uses obi vwap momentum strategy)
add_strategy_target(HFT "trading::ObiVwapMomentumStrategy" "ObiVwapMomentumStrategy")

add_subdirectory(repository)
if (ENABLE_TESTS)
    set(TEST_STRATEGY_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/tests")
    file(MAKE_DIRECTORY "${TEST_STRATEGY_INCLUDE_DIR}")
    set(STRATEGY_TYPE "trading::ObiVwapMomentumStrategy")
    set(STRATEGY_NAME "ObiVwapMomentumStrategy")
    configure_file("${STRATEGY_CONFIG_TEMPLATE}"
            "${TEST_STRATEGY_INCLUDE_DIR}/strategy_config.hpp" @ONLY)
    add_subdirectory(test)
endif ()

set(HFT_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/hft/config")
set(RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")

file(COPY ${RESOURCES_DIR} DESTINATION ${CMAKE_BINARY_DIR})
file(COPY util/set_cpu.sh DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${RESOURCES_DIR} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})