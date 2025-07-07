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