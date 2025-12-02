include(FetchContent)

set(ABSL_BUILD_TESTING OFF CACHE BOOL "Disable Abseil tests" FORCE)
set(ABSL_BUILD_TEST_HELPERS OFF CACHE BOOL "Disable Abseil test helpers" FORCE)

FetchContent_Declare(
        abseil
        GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
        GIT_TAG        20250512.1
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
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/CpuManager/*.cpp"
)

add_library(Common STATIC ${LIB_SRC_FILES})
add_library(concurrentqueue INTERFACE)
target_include_directories(concurrentqueue INTERFACE
        ${CMAKE_SOURCE_DIR}/extern/concurrentqueue)

target_precompile_headers(Common PUBLIC ${CMAKE_SOURCE_DIR}/include/pch.h)
target_include_directories(Common PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(Common
        PUBLIC
        absl::flat_hash_map
        concurrentqueue
)