add_subdirectory(common)
add_subdirectory(util)

add_library(HftLib STATIC
        hft_lib.cpp
)

target_include_directories(HftLib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/common
        ${CMAKE_CURRENT_SOURCE_DIR}/util
)

target_link_libraries(HftLib PUBLIC
        Common
        Util
)