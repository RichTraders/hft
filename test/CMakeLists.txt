set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(cmake/googletest.cmake)
include(cmake/google-benchmark.cmake)

enable_testing()
set(TEST_TARGETS "")

set(TEST_RUNTIME_DIR "${CMAKE_BINARY_DIR}/test")

file(MAKE_DIRECTORY "${TEST_RUNTIME_DIR}")

macro(add_unit_test _TEST_NAME)
    if (NOT TEST_STRATEGY_INCLUDE_DIR)
        message(FATAL_ERROR "TEST_STRATEGY_INCLUDE_DIR is not defined. Enable tests via top-level option.")
    endif ()

    if (${_TEST_NAME} MATCHES "^benchmark_")
        set(TARGET_NAME "${_TEST_NAME}_benchmarks")
    else ()
        set(TARGET_NAME "${_TEST_NAME}_tests")
    endif ()

    add_executable(${TARGET_NAME} ${_TEST_NAME}_tests.cpp)

    set(BASE_LIBS "")
    if (NOT APPLE)
        set(BASE_LIBS cap)
    endif()

    if (${_TEST_NAME} MATCHES "^benchmark_")
        target_link_libraries(${TARGET_NAME} PRIVATE
                HftLib_ws
                ${BASE_LIBS}
                benchmark::benchmark
                benchmark::benchmark_main
        )
        # Add WebSocket include directories for benchmark tests
        if(ENABLE_WEBSOCKET)
            target_include_directories(${TARGET_NAME} PRIVATE
                    ${CMAKE_SOURCE_DIR}/hft/core/websocket
                    ${CMAKE_SOURCE_DIR}/hft/core/websocket/market_data
            )
        endif()
    elseif (${_TEST_NAME} MATCHES "^fix_")
        target_link_libraries(${TARGET_NAME} PRIVATE
                HftLib_fix
                GTest::gtest_main
                GTest::gmock
                ${BASE_LIBS}
        )
    else ()
        target_compile_definitions(${TARGET_NAME} PRIVATE ENABLE_WEBSOCKET)
        target_link_libraries(${TARGET_NAME} PRIVATE
                HftLib_ws
                GTest::gtest_main
                GTest::gmock
                ${BASE_LIBS}
        )
    endif ()
    target_precompile_headers(${TARGET_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/include/pch.h)
    target_include_directories(${TARGET_NAME} PRIVATE
            ${TEST_STRATEGY_INCLUDE_DIR})
    if (NOT ${_TEST_NAME} MATCHES "^benchmark_")
        add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME} WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
    endif ()
    target_compile_definitions(${TARGET_NAME} PRIVATE UNIT_TEST)
    target_compile_options(${TARGET_NAME} PRIVATE
            -Wno-missing-field-initializers
            -Wno-error=unused-parameter
            -Wno-error=unused-function
            -Wno-error=unused-variable
    )

    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${TARGET_NAME} PRIVATE
                -Wno-error=return-type-c-linkage
        )
    endif()

    # ASan flags are now set globally in root CMakeLists.txt
    if (ENABLE_ASAN)
        # GCC uses -Wno-maybe-uninitialized, Clang uses -Wno-uninitialized
        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${TARGET_NAME} PRIVATE -Wno-error=maybe-uninitialized -Wno-maybe-uninitialized)
        elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${TARGET_NAME} PRIVATE -Wno-error=uninitialized -Wno-uninitialized)
        endif()
    endif ()

    #list(APPEND TEST_TARGETS ${TARGET_NAME})
    if (${_TEST_NAME} MATCHES "^benchmark_")
        list(APPEND BENCHMARK_TARGETS ${TARGET_NAME})
    else()
        list(APPEND TEST_TARGETS ${TARGET_NAME})
    endif()
endmacro()

add_definitions(-DGOOGLEMOCK_INCLUDE_GMOCK_INTERNAL_GMOCK_GENERATED_FUNCTION_MOCKER_H_)

# Include mock utilities directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/mock)

# Auto-discover unit tests (exclude integration tests and pipeline benchmark tests)
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*_tests.cpp")
list(FILTER TEST_SOURCES EXCLUDE REGEX ".*cpu_cgroup_integration_tests\\.cpp$")
list(FILTER TEST_SOURCES EXCLUDE REGEX ".*full_pipeline_benchmark_tests\\.cpp$")

# Exclude fix_ tests and FIX benchmark tests when ENABLE_WEBSOCKET is ON
if(ENABLE_WEBSOCKET)
    list(FILTER TEST_SOURCES EXCLUDE REGEX ".*fix_.*_tests\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX ".*benchmark_decoder_performance_fix_tests\\.cpp$")
    # Exclude broken benchmark tests that reference non-existent json_md_decoder.hpp
    list(FILTER TEST_SOURCES EXCLUDE REGEX ".*benchmark_decoder_performance_json_tests\\.cpp$")
    list(FILTER TEST_SOURCES EXCLUDE REGEX ".*benchmark_fixed_point_tests\\.cpp$")
endif()

# Exclude Linux-specific tests on macOS
if(APPLE)
    list(FILTER TEST_SOURCES EXCLUDE REGEX ".*cpu_priority_tests\\.cpp$")
endif()

foreach (test_src IN LISTS TEST_SOURCES)
    get_filename_component(fname "${test_src}" NAME_WE)
    string(REGEX REPLACE "_tests$" "" test_name "${fname}")
    add_unit_test(${test_name})
endforeach ()

# Strategy-specific full_pipeline_benchmark tests
macro(add_strategy_benchmark strategy_type strategy_name)
    set(TARGET_NAME "full_pipeline_benchmark_${strategy_name}_tests")
    set(gen_dir "${CMAKE_BINARY_DIR}/generated/test_${strategy_name}")
    file(MAKE_DIRECTORY "${gen_dir}")

    set(STRATEGY_TYPE "${strategy_type}")
    set(STRATEGY_NAME "${strategy_name}")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/strategy_config.hpp.in"
            "${gen_dir}/strategy_config.hpp" @ONLY)

    add_executable(${TARGET_NAME} full_pipeline_benchmark_tests.cpp)
    target_include_directories(${TARGET_NAME} PRIVATE "${gen_dir}")
    target_compile_definitions(${TARGET_NAME} PRIVATE ENABLE_WEBSOCKET UNIT_TEST)

    set(STRATEGY_LIBS HftLib_ws GTest::gtest_main GTest::gmock)
    if (NOT APPLE)
        list(APPEND STRATEGY_LIBS cap)
    endif()

    target_link_libraries(${TARGET_NAME} PRIVATE ${STRATEGY_LIBS})
    target_precompile_headers(${TARGET_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/include/pch.h)
    target_compile_options(${TARGET_NAME} PRIVATE
            -Wno-missing-field-initializers
            -Wno-error=unused-parameter
            -Wno-error=unused-function
            -Wno-error=unused-variable
    )
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${TARGET_NAME} PRIVATE -Wno-error=return-type-c-linkage)
    endif()

    list(APPEND BENCHMARK_TARGETS ${TARGET_NAME})
endmacro()

add_strategy_benchmark("trading::ObiVwapDirectionalStrategy" "directional")
add_strategy_benchmark("trading::MeanReversionMakerStrategy" "mean_reversion")
add_strategy_benchmark("trading::LiquidTaker" "liquid_taker")

# Add integration tests with special handling
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cpu_cgroup_integration_tests.cpp")
    add_unit_test(cpu_cgroup_integration)

    set_tests_properties(cpu_cgroup_integration_tests PROPERTIES
            LABELS "integration;requires_systemd"
    )

    file(COPY run_integration_test.sh
            DESTINATION ${CMAKE_BINARY_DIR}/test
            FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif ()

file(COPY set_cpu.sh DESTINATION ${CMAKE_BINARY_DIR}/test)

add_custom_target(RUN_ALL_TESTS
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG>
        WORKING_DIRECTORY "${TEST_RUNTIME_DIR}"
        DEPENDS ${TEST_TARGETS}
)

add_custom_target(RUN_INTEGRATION_TESTS
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_integration_test.sh
        ${TEST_RUNTIME_DIR}/cpu_cgroup_integration_tests
        WORKING_DIRECTORY "${TEST_RUNTIME_DIR}"
        DEPENDS cpu_cgroup_integration_tests
        COMMENT "Running integration tests in iso.slice with systemd-run"
)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources DESTINATION "${TEST_RUNTIME_DIR}")

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data DESTINATION "${TEST_RUNTIME_DIR}")