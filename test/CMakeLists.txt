set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(cmake/googletest.cmake)

enable_testing()

macro(add_unit_test _TEST_NAME)
    add_executable(${_TEST_NAME}_tests ${_TEST_NAME}_tests.cpp)
    target_link_libraries(${_TEST_NAME}_tests PRIVATE
            HftLib
            Core
            gtest
            GTest::gtest_main
            gmock
            gmock_main
    )
    target_precompile_headers(${_TEST_NAME}_tests PRIVATE
            ${CMAKE_SOURCE_DIR}/include/pch.h
            ${CMAKE_SOURCE_DIR}/hft/hft_lib.h)
    add_test(NAME ${_TEST_NAME}_tests COMMAND ${_TEST_NAME}_tests WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/test)
    target_compile_definitions(${_TEST_NAME}_tests PRIVATE UNIT_TEST)
    target_compile_options(${_TEST_NAME}_tests PRIVATE -Wno-missing-field-initializers -Wno-error=unused-parameter)
endmacro()

add_definitions(-DGOOGLEMOCK_INCLUDE_GMOCK_INTERNAL_GMOCK_GENERATED_FUNCTION_MOCKER_H_)

add_unit_test(spsc)
add_unit_test(thread)
add_unit_test(fix_app)
add_unit_test(fix_md_core)
add_unit_test(fix_oe_core)
add_unit_test(logger)
add_unit_test(mpsc)
add_unit_test(memory_pool)
add_unit_test(order_book)
add_unit_test(feature_engine)
add_unit_test(position_keeper)
add_unit_test(risk_manager)
add_unit_test(ini_config)
add_unit_test(order_gateway)


set(RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/resources")
file(COPY ${RESOURCE_DIR} DESTINATION ${CMAKE_BINARY_DIR}/test)

file(GLOB TEST_EXECUTABLES "${CMAKE_BINARY_DIR}/tests/*_tests")

add_custom_target(RUN_ALL_TESTS
        COMMAND sudo ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS ${TEST_EXECUTABLES}
)