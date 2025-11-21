set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(cmake/googletest.cmake)

enable_testing()
set(TEST_TARGETS "")

set(TEST_RUNTIME_DIR "${CMAKE_BINARY_DIR}/test")

file(MAKE_DIRECTORY "${TEST_RUNTIME_DIR}")

option(ENABLE_ASAN "Build with AddressSanitizer" OFF)

macro(add_unit_test _TEST_NAME)
    add_executable(${_TEST_NAME}_tests ${_TEST_NAME}_tests.cpp)
    if (NOT TEST_STRATEGY_INCLUDE_DIR)
        message(FATAL_ERROR "TEST_STRATEGY_INCLUDE_DIR is not defined. Enable tests via top-level option.")
    endif ()
    target_link_libraries(${_TEST_NAME}_tests PRIVATE
            HftLib
            Core
            gtest
            GTest::gtest_main
            gmock
            gmock_main
            cap
    )
    target_precompile_headers(${_TEST_NAME}_tests PRIVATE
            ${CMAKE_SOURCE_DIR}/include/pch.h
            ${CMAKE_SOURCE_DIR}/hft/hft_lib.h)
    target_include_directories(${_TEST_NAME}_tests PRIVATE
            ${TEST_STRATEGY_INCLUDE_DIR})
    add_test(NAME ${_TEST_NAME}_tests COMMAND ${_TEST_NAME}_tests WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
    target_compile_definitions(${_TEST_NAME}_tests PRIVATE UNIT_TEST)
    target_compile_options(${_TEST_NAME}_tests PRIVATE
            -Wno-missing-field-initializers
            -Wno-error=unused-parameter
            -Wno-error=unused-function
            -Wno-error=return-type-c-linkage)

    if (ENABLE_ASAN)
        message(STATUS "Building with AddressSanitizer")
        #target_compile_options(${_TEST_NAME}_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer -O1 -g -Wno-error=maybe-uninitialized -Wno-maybe-uninitialized)
        target_compile_options(${_TEST_NAME}_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer -g -Wno-error=maybe-uninitialized -Wno-maybe-uninitialized)
        target_link_options(${_TEST_NAME}_tests PRIVATE -fsanitize=address)
    endif()

    list(APPEND TEST_TARGETS ${_TEST_NAME}_tests)
endmacro()

add_definitions(-DGOOGLEMOCK_INCLUDE_GMOCK_INTERNAL_GMOCK_GENERATED_FUNCTION_MOCKER_H_)

# Auto-discover unit tests (exclude integration tests)
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*_tests.cpp")
list(FILTER TEST_SOURCES EXCLUDE REGEX ".*cpu_cgroup_integration_tests\\.cpp$")

foreach(test_src IN LISTS TEST_SOURCES)
    get_filename_component(fname "${test_src}" NAME_WE)
    string(REGEX REPLACE "_tests$" "" test_name "${fname}")
    add_unit_test(${test_name})
endforeach()

# Add integration tests with special handling
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cpu_cgroup_integration_tests.cpp")
    add_unit_test(cpu_cgroup_integration)

    # Override the test command to use wrapper script for systemd-run
    set_tests_properties(cpu_cgroup_integration_tests PROPERTIES
        LABELS "integration;requires_systemd"
    )

    # Make wrapper script executable
    file(COPY run_integration_test.sh
         DESTINATION ${CMAKE_BINARY_DIR}/test
         FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()

file(COPY set_cpu.sh DESTINATION ${CMAKE_BINARY_DIR}/test)

add_custom_target(RUN_ALL_TESTS
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG>
        WORKING_DIRECTORY "${TEST_RUNTIME_DIR}"
        DEPENDS ${TEST_TARGETS}
)

# Target to run integration tests with systemd-run
add_custom_target(RUN_INTEGRATION_TESTS
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/run_integration_test.sh
                ${TEST_RUNTIME_DIR}/cpu_cgroup_integration_tests
        WORKING_DIRECTORY "${TEST_RUNTIME_DIR}"
        DEPENDS cpu_cgroup_integration_tests
        COMMENT "Running integration tests in iso.slice with systemd-run"
)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources DESTINATION "${TEST_RUNTIME_DIR}")
