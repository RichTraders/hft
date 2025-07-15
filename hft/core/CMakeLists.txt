add_subdirectory(NewOroFix44)

file(GLOB_RECURSE LIB_SRC_FILES
        dummy.cpp
)

add_library(Core STATIC ${LIB_SRC_FILES})
target_precompile_headers(Core PUBLIC ${CMAKE_SOURCE_DIR}/include/pch.h)
target_include_directories(Core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/NewOroFix44
)
target_link_libraries(Core PUBLIC Fix8Schema)