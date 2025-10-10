set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(cmake/googletest.cmake)

enable_testing()
set(TEST_TARGETS "")

set(TEST_RUNTIME_DIR "${CMAKE_BINARY_DIR}/test")

set(RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
file(MAKE_DIRECTORY "${TEST_RUNTIME_DIR}")
file(COPY "${RESOURCE_DIR}" DESTINATION "${TEST_RUNTIME_DIR}")

option(ENABLE_ASAN "Build with AddressSanitizer" OFF)

macro(add_unit_test _TEST_NAME)
    add_executable(${_TEST_NAME}_tests ${_TEST_NAME}_tests.cpp)
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
    add_test(NAME ${_TEST_NAME}_tests COMMAND ${_TEST_NAME}_tests WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
    target_compile_definitions(${_TEST_NAME}_tests PRIVATE UNIT_TEST)
    target_compile_options(${_TEST_NAME}_tests PRIVATE -Wno-missing-field-initializers -Wno-error=unused-parameter -Wno-error=unused-function)

    if (ENABLE_ASAN)
        message(STATUS "Building with AddressSanitizer")
        #target_compile_options(${_TEST_NAME}_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer -O1 -g -Wno-error=maybe-uninitialized -Wno-maybe-uninitialized)
        target_compile_options(${_TEST_NAME}_tests PRIVATE -fsanitize=address -fno-omit-frame-pointer -g -Wno-error=maybe-uninitialized -Wno-maybe-uninitialized)
        target_link_options(${_TEST_NAME}_tests PRIVATE -fsanitize=address)
    endif()

    list(APPEND TEST_TARGETS ${_TEST_NAME}_tests)
endmacro()

add_definitions(-DGOOGLEMOCK_INCLUDE_GMOCK_INTERNAL_GMOCK_GENERATED_FUNCTION_MOCKER_H_)

add_unit_test(spsc)
add_unit_test(thread)
add_unit_test(fix_md_core)
add_unit_test(fix_oe_core)
add_unit_test(logger)
#add_unit_test(mpsc)
add_unit_test(memory_pool)
add_unit_test(order_book)
add_unit_test(feature_engine)
add_unit_test(position_keeper)
add_unit_test(risk_manager)
add_unit_test(ini_config)
add_unit_test(order_gateway)
add_unit_test(fix_md_app)
add_unit_test(cpu_priority)
add_unit_test(layer_book)
add_unit_test(quote_reconcile)
add_unit_test(extract)
add_unit_test(logger_performance)

file(COPY set_cpu.sh DESTINATION ${CMAKE_BINARY_DIR}/test)

add_custom_target(RUN_ALL_TESTS
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -C $<CONFIG>
        WORKING_DIRECTORY "${TEST_RUNTIME_DIR}"
        DEPENDS ${TEST_TARGETS}
)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources DESTINATION ${CMAKE_CURRENT_BINARY_DIR})