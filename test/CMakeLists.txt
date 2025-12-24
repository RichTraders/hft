set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(cmake/googletest.cmake)
include(cmake/google-benchmark.cmake)

enable_testing()
set(TEST_TARGETS "")

set(TEST_RUNTIME_DIR "${CMAKE_BINARY_DIR}/test")

file(MAKE_DIRECTORY "${TEST_RUNTIME_DIR}")

option(ENABLE_ASAN "Build with AddressSanitizer" OFF)

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

    set(BASE_LIBS cap)

    if (${_TEST_NAME} MATCHES "^benchmark_")
        target_link_libraries(${TARGET_NAME} PRIVATE
                HftLib_ws HftLib_fix
                ${BASE_LIBS}
                benchmark::benchmark
                benchmark::benchmark_main
        )
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
            -Wno-error=return-type-c-linkage)

    if (ENABLE_ASAN)
        message(STATUS "Building with AddressSanitizer")
        target_compile_options(${TARGET_NAME} PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
        # GCC uses -Wno-maybe-uninitialized, Clang uses -Wno-uninitialized
        if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${TARGET_NAME} PRIVATE -Wno-error=maybe-uninitialized -Wno-maybe-uninitialized)
        elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${TARGET_NAME} PRIVATE -Wno-error=uninitialized -Wno-uninitialized)
        endif()
        target_link_options(${TARGET_NAME} PRIVATE -fsanitize=address)
    endif ()

    #list(APPEND TEST_TARGETS ${TARGET_NAME})
    if (${_TEST_NAME} MATCHES "^benchmark_")
        list(APPEND BENCHMARK_TARGETS ${TARGET_NAME})
    else()
        list(APPEND TEST_TARGETS ${TARGET_NAME})
    endif()
endmacro()

add_definitions(-DGOOGLEMOCK_INCLUDE_GMOCK_INTERNAL_GMOCK_GENERATED_FUNCTION_MOCKER_H_)

# Auto-discover unit tests (exclude integration tests)
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*_tests.cpp")
list(FILTER TEST_SOURCES EXCLUDE REGEX ".*cpu_cgroup_integration_tests\\.cpp$")

foreach (test_src IN LISTS TEST_SOURCES)
    get_filename_component(fname "${test_src}" NAME_WE)
    string(REGEX REPLACE "_tests$" "" test_name "${fname}")
    add_unit_test(${test_name})
endforeach ()

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