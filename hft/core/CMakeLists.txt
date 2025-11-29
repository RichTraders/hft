file(GLOB LIB_SRC_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/*.tpp
)

if(ENABLE_WEBSOCKET)
    file(GLOB WEBSOCKET_SRC_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/websocket/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/websocket/*.tpp
    )
    list(APPEND LIB_SRC_FILES ${WEBSOCKET_SRC_FILES})
else()
    add_subdirectory(NewOroFix44)
    file(GLOB FIX_SRC_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/fix/*.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/fix/*.tpp
    )
    list(APPEND LIB_SRC_FILES ${FIX_SRC_FILES})
endif()

add_library(Core STATIC ${LIB_SRC_FILES})
target_precompile_headers(Core PUBLIC ${CMAKE_SOURCE_DIR}/include/pch.h)
target_include_directories(Core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/common
        ${CMAKE_SOURCE_DIR}/include
)
target_link_libraries(Core PUBLIC Common)
if(ENABLE_WEBSOCKET)
    target_link_libraries(Core PUBLIC
            websockets
            glaze::glaze
    )
#    target_include_directories(Core PRIVATE
#            ${libwebsockets_SOURCE_DIR}/include      # libwebsockets.h
#            ${libwebsockets_BINARY_DIR}/include      # lws_config.h (generated)
#    )
else()
    target_include_directories(Core SYSTEM PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/NewOroFix44
    )
    target_compile_options(Core PRIVATE
            -Wno-missing-field-initializers
            -Wno-unused-parameter
            -Wno-error=unused-parameter
            -Wno-return-type-c-linkage
            -Wno-error=return-type-c-linkage
    )
    target_link_libraries(Core PUBLIC Fix8Schema)
endif()
