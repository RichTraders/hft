file(GLOB_RECURSE LIB_SRC_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
)

if(ENABLE_WEBSOCKET)
    add_library(Src_ws STATIC ${LIB_SRC_FILES})
    target_include_directories(Src_ws PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(Src_ws PUBLIC Core_ws)
    target_compile_definitions(Src_ws PRIVATE ENABLE_WEBSOCKET)
else()
    add_library(Src_fix STATIC ${LIB_SRC_FILES})
    target_include_directories(Src_fix PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(Src_fix PRIVATE Core_fix)
endif()

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/strategy")
  file(GLOB_RECURSE STRATEGY_CONFIG_FILES "${CMAKE_CURRENT_SOURCE_DIR}/strategy/*.ini")
  foreach (config_file ${STRATEGY_CONFIG_FILES})
    file(COPY ${config_file} DESTINATION "${CMAKE_BINARY_DIR}/resources/strategy")
  endforeach ()
endif ()