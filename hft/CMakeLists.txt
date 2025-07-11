add_subdirectory(common)

add_library(HftLib STATIC
        hft_lib.cpp
)

target_include_directories(HftLib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(HftLib PUBLIC
        Common
)