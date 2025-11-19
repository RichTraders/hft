file(GLOB_RECURSE LIB_SRC_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.tpp
        ${CMAKE_CURRENT_SOURCE_DIR}/strategy/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/strategy/*.tpp
)

add_library(Src STATIC
        ${LIB_SRC_FILES}
)
target_include_directories(Src PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(Src
        PRIVATE
        Core
)