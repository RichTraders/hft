include(FetchContent)

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
)

list(FILTER LIB_SRC_FILES EXCLUDE REGEX ".*/cpu_manager_linux\\.cpp$")
list(FILTER LIB_SRC_FILES EXCLUDE REGEX ".*/cpu_manager_macos\\.cpp$")

add_library(Common STATIC ${LIB_SRC_FILES})

# ⬇️ 여기서 OS에 맞는 구현 파일을 다시 '선택적으로' 추가
target_sources(Common PRIVATE
        $<$<PLATFORM_ID:Linux>: ${CMAKE_CURRENT_SOURCE_DIR}/cpu_manager_linux.cpp>
        $<$<PLATFORM_ID:Darwin>:${CMAKE_CURRENT_SOURCE_DIR}/cpu_manager_macos.cpp>
)

target_precompile_headers(Common PUBLIC ${CMAKE_SOURCE_DIR}/include/pch.h)
target_include_directories(Common PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(Common
        PUBLIC
        absl::flat_hash_map
)