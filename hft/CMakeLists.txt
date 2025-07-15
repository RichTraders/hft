add_subdirectory(common)
add_subdirectory(core)
add_subdirectory(src)

add_library(HftLib STATIC
        hft_lib.cpp
)

target_include_directories(HftLib PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/common
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(HftLib PUBLIC
        Common
        Core
        Src
)