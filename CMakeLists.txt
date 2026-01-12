cmake_minimum_required(VERSION 3.28)

# Use Homebrew LLVM on macOS for full C++17 from_chars support
if (APPLE AND EXISTS "/opt/homebrew/opt/llvm/bin/clang++")
    set(CMAKE_C_COMPILER "/opt/homebrew/opt/llvm/bin/clang")
    set(CMAKE_CXX_COMPILER "/opt/homebrew/opt/llvm/bin/clang++")
    # Set sysroot to avoid SDK header conflicts
    execute_process(
        COMMAND xcrun --show-sdk-path
        OUTPUT_VARIABLE MACOS_SDK_PATH
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(CMAKE_OSX_SYSROOT "${MACOS_SDK_PATH}")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3")
    # Disable C++20 module scanning (conflicts with Ninja + LLVM)
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
    # Use LLVM's libc++ to avoid ABI mismatch
    set(LLVM_ROOT "/opt/homebrew/opt/llvm")
    add_compile_options(-stdlib=libc++ -nostdinc++)
    add_compile_options(-isystem ${LLVM_ROOT}/include/c++/v1)
    add_link_options(-stdlib=libc++ -L${LLVM_ROOT}/lib/c++ -Wl,-rpath,${LLVM_ROOT}/lib/c++)
endif ()

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
option(LOGGER_PERF_TRACE "Enable logger performance tracing with RDTSC" OFF)
option(ENABLE_ASAN "Build with AddressSanitizer" OFF)
option(USE_ONEPASS_DECODER "Use OnepassBinanceFuturesMdDecoder instead of JSON decoder" ON)
option(ENABLE_CONVERTER_TIMING "Enable timing logging in domain converter" OFF)
option(USE_FLAT_ORDERBOOK "Use flat array based orderbook instead of bucket based" OFF)

# FIX protocol requires fix8 library which is not available on macOS
if(APPLE)
    option(SKIP_FIX_BUILD "Skip FIX protocol build (fix8 not available)" ON)
else()
    option(SKIP_FIX_BUILD "Skip FIX protocol build" OFF)
endif()
set(FIXED_POINT_SYMBOL "XRPUSDC" CACHE STRING "Target symbol for FixedPoint config")

if (ENABLE_ASAN)
    message(STATUS "Building with AddressSanitizer (global)")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
endif ()

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

if (LOGGER_PERF_TRACE)
    add_compile_definitions(LOGGER_PERF_TRACE)
    message(STATUS "Logger performance tracing enabled (RDTSC)")
endif ()

if (USE_FLAT_ORDERBOOK)
    add_compile_definitions(USE_FLAT_ORDERBOOK)
    message(STATUS "Using flat array based orderbook")
else ()
    message(STATUS "Using bucket based orderbook (default)")
endif ()

if (USE_ONEPASS_DECODER)
    add_compile_definitions(USE_ONEPASS_DECODER)
    message(STATUS "Using OnepassBinanceFuturesMdDecoder for market data")
else ()
    message(STATUS "Using JsonBinanceFuturesMdDecoder for market data (default)")
endif ()

if (ENABLE_CONVERTER_TIMING)
    add_compile_definitions(ENABLE_CONVERTER_TIMING)
    message(STATUS "Converter timing logging enabled")
endif ()

# FixedPoint config (int64_t based arithmetic - always enabled)
if (FIXED_POINT_SYMBOL STREQUAL "XRPUSDC")
    add_compile_definitions(FIXED_POINT_SYMBOL_XRPUSDC)
    message(STATUS "FixedPoint: XRPUSDC (price=4, qty=1)")
else ()
    message(STATUS "FixedPoint: BTCUSDC (price=1, qty=3) [default]")
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

add_strategy_target(HFT_maker "trading::ObiVwapDirectionalStrategy" "ObiVwapDirectionalStrategy")
add_strategy_target(HFT_taker "trading::LiquidTaker" "LiquidTaker")
add_strategy_target(HFT_mean_reversion "trading::MeanReversionMakerStrategy" "MeanReversionMakerStrategy")
# Default target for backward compatibility (uses obi vwap directional strategy)
add_strategy_target(HFTMain "trading::ObiVwapDirectionalStrategy" "ObiVwapDirectionalStrategy")

add_subdirectory(repository)
if (ENABLE_TESTS)
    set(TEST_STRATEGY_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated/tests")
    file(MAKE_DIRECTORY "${TEST_STRATEGY_INCLUDE_DIR}")
    set(STRATEGY_TYPE "trading::MeanReversionMakerStrategy")
    set(STRATEGY_NAME "MeanReversionMakerStrategy")
    configure_file("${STRATEGY_CONFIG_TEMPLATE}"
            "${TEST_STRATEGY_INCLUDE_DIR}/strategy_config.hpp" @ONLY)
    add_subdirectory(test)
endif ()

set(HFT_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/hft/config")
set(RESOURCES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")

file(COPY ${RESOURCES_DIR} DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${RESOURCES_DIR} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})