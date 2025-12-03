cmake_minimum_required(VERSION 3.28)
project(repository)
# [HACK] build with all source files

#core
set(FIX8_SRC
        ${CMAKE_SOURCE_DIR}/hft/core/fix/NewOroFix44/NewOroFix44MD_types.hpp
        ${CMAKE_SOURCE_DIR}/hft/core/fix/NewOroFix44/NewOroFix44MD_types.cpp
        ${CMAKE_SOURCE_DIR}/hft/core/fix/NewOroFix44/NewOroFix44MD_traits.cpp
        ${CMAKE_SOURCE_DIR}/hft/core/fix/NewOroFix44/NewOroFix44MD_router.hpp
        ${CMAKE_SOURCE_DIR}/hft/core/fix/NewOroFix44/NewOroFix44MD_classes.hpp
        ${CMAKE_SOURCE_DIR}/hft/core/fix/NewOroFix44/NewOroFix44MD_classes.cpp

        ${CMAKE_SOURCE_DIR}/hft/core/fix/NewOroFix44/NewOroFix44OE_types.hpp
        ${CMAKE_SOURCE_DIR}/hft/core/fix/NewOroFix44/NewOroFix44OE_types.cpp
        ${CMAKE_SOURCE_DIR}/hft/core/fix/NewOroFix44/NewOroFix44OE_traits.cpp
        ${CMAKE_SOURCE_DIR}/hft/core/fix/NewOroFix44/NewOroFix44OE_router.hpp
        ${CMAKE_SOURCE_DIR}/hft/core/fix/NewOroFix44/NewOroFix44OE_classes.hpp
        ${CMAKE_SOURCE_DIR}/hft/core/fix/NewOroFix44/NewOroFix44OE_classes.cpp

        ${CMAKE_SOURCE_DIR}/hft/core/fix/fix_md_core.cpp
        ${CMAKE_SOURCE_DIR}/hft/core/fix/fix_oe_core.cpp
        ${CMAKE_SOURCE_DIR}/hft/core/fix/fix_app.cpp
        ${CMAKE_SOURCE_DIR}/hft/core/fix/fix_md_app.cpp
        ${CMAKE_SOURCE_DIR}/hft/core/fix/fix_oe_app.cpp
        ${CMAKE_SOURCE_DIR}/hft/core/fix/fix_sequence_counter.cpp
        ${CMAKE_SOURCE_DIR}/hft/core/ssl_socket.cpp
        ${CMAKE_SOURCE_DIR}/hft/core/signature.cpp
        ${CMAKE_SOURCE_DIR}/hft/core/response_manager.cpp
)

add_library(BrokerFix8Schema STATIC ${FIX8_SRC})
target_precompile_headers(BrokerFix8Schema PUBLIC ${CMAKE_SOURCE_DIR}/include/pch.h)
target_include_directories(BrokerFix8Schema PUBLIC
        ${CMAKE_SOURCE_DIR}/hft/core
        ${CMAKE_SOURCE_DIR}/hft/core/fix/NewOroFix44
        ${CMAKE_SOURCE_DIR}/hft/core/fix
        /usr/local/include
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/hft
)
target_compile_definitions(BrokerFix8Schema PUBLIC REPOSITORY)
target_link_directories(BrokerFix8Schema PUBLIC /usr/local/lib)
target_link_libraries(BrokerFix8Schema PUBLIC
        fix8
        Common
        z
        PocoFoundation
        PocoNet
        stdc++
        gcc_s
        pcre
        m
        ssl
        crypto
)

target_compile_options(BrokerFix8Schema PUBLIC
        -Wno-missing-field-initializers
        -Wno-unused-parameter
        -Wno-error=unused-parameter
)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(BrokerFix8Schema PUBLIC
            -Wno-return-type-c-linkage
            -Wno-error=return-type-c-linkage
    )
endif()
############################################

#common
FetchContent_Declare(
        abseil
        GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
        GIT_TAG 20250512.1
)

FetchContent_MakeAvailable(abseil)
set(_no_ignored_attr
        $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wno-ignored-attributes -Wno-error=ignored-attributes>)

if (TARGET absl_crc32c)
    target_compile_options(absl_crc32c PRIVATE ${_no_ignored_attr})
endif()

foreach(t IN ITEMS absl_crc_internal absl_crc_cpu_detect)
    if (TARGET ${t})
        target_compile_options(${t} PRIVATE ${_no_ignored_attr})
    endif()
endforeach()

file(GLOB_RECURSE LIB_SRC_FILES
        "${CMAKE_SOURCE_DIR}/hft/common/*.cpp"
        "${CMAKE_SOURCE_DIR}/hft/common/*.hpp"
)

add_library(BrokerCommon STATIC ${LIB_SRC_FILES})

target_precompile_headers(BrokerCommon PUBLIC ${CMAKE_SOURCE_DIR}/include/pch.h)
target_include_directories(BrokerCommon PUBLIC
        ${CMAKE_SOURCE_DIR}/hft/common
        ${CMAKE_SOURCE_DIR}/include
)
target_compile_definitions(BrokerCommon PUBLIC LOGGER_PREFIX_DISABLED)
target_link_libraries(BrokerCommon
        PUBLIC
        absl::flat_hash_map
)
target_include_directories(BrokerCommon PRIVATE
        ${CMAKE_SOURCE_DIR}/extern/concurrentqueue)
######################################

set(CMAKE_CXX_STANDARD 20)
add_compile_options(-Wall -Wextra -Wpedantic -Werror)
if (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-march=native)
endif ()

add_executable(Repository main.cpp broker.cpp)
target_compile_definitions(Repository PUBLIC LIGHT_LOGGER)
target_precompile_headers(Repository PUBLIC ${CMAKE_SOURCE_DIR}/include/pch.h)
target_link_libraries(Repository PRIVATE
        BrokerFix8Schema
        BrokerCommon
)


set(RESOURCES_DIR "${CMAKE_SOURCE_DIR}/resources")
file(COPY ${RESOURCES_DIR} DESTINATION ${CMAKE_BINARY_DIR}/repository)