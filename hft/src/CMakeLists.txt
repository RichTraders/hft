file(GLOB_RECURSE LIB_SRC_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.tpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/strategy/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/strategy/*.tpp"
)

add_library(Src_ws STATIC ${LIB_SRC_FILES})
target_include_directories(Src_ws PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Src_ws PUBLIC Core_ws)
target_compile_definitions(Src_ws PRIVATE ENABLE_WEBSOCKET)

add_library(Src_fix STATIC ${LIB_SRC_FILES})
target_include_directories(Src_fix PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(Src_fix PRIVATE Core_fix)
