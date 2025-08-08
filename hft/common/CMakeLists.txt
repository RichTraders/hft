include(FetchContent)

FetchContent_Declare(
        abseil
        GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
        GIT_TAG        20250512.1
)

FetchContent_MakeAvailable(abseil)

file(GLOB_RECURSE LIB_SRC_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

add_library(Common STATIC ${LIB_SRC_FILES})

target_precompile_headers(Common PUBLIC ${CMAKE_SOURCE_DIR}/include/pch.h)
target_include_directories(Common PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(Common
        PUBLIC
        absl::flat_hash_map
)