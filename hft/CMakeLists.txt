add_subdirectory(common)
add_subdirectory(core)
add_subdirectory(src)

option(ENABLE_REPOSITORY "Enable repository build" OFF)

add_library(HftLib_ws STATIC
        hft_lib.cpp
)
target_include_directories(HftLib_ws PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/common
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(HftLib_ws PUBLIC
        Common
        Core_ws
        Src_ws
)
target_compile_definitions(HftLib_ws PRIVATE
    ENABLE_WEBSOCKET
)

add_library(HftLib_fix STATIC
        hft_lib.cpp
)
target_include_directories(HftLib_fix PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/common
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(HftLib_fix PUBLIC
        Common
        Core_fix
        Src_fix
)