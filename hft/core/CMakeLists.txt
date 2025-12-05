file(GLOB CORE_COMMON_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.tpp"
)

file(GLOB CORE_WS_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/websocket/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/websocket/*.tpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/websocket/market_data/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/websocket/order_entry/*.cpp"
)
add_library(Core_ws STATIC ${CORE_COMMON_SRC} ${CORE_WS_SRC})
target_precompile_headers(Core_ws PUBLIC ${CMAKE_SOURCE_DIR}/include/pch.h)
target_include_directories(Core_ws PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/websocket
)
target_link_libraries(Core_ws PUBLIC Common websockets glaze::glaze)
target_compile_definitions(Core_ws PRIVATE ENABLE_WEBSOCKET)

add_subdirectory(fix/NewOroFix44)
file(GLOB CORE_FIX_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/fix/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/fix/*.tpp"
)
add_library(Core_fix STATIC ${CORE_COMMON_SRC} ${CORE_FIX_SRC})
target_precompile_headers(Core_fix PUBLIC ${CMAKE_SOURCE_DIR}/include/pch.h)
target_include_directories(Core_fix PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_options(Core_fix PRIVATE
    -Wno-missing-field-initializers
    -Wno-unused-parameter
    -Wno-error=unused-parameter
    -Wno-return-type-c-linkage
    -Wno-error=return-type-c-linkage
)
target_link_libraries(Core_fix PUBLIC Common Fix8Schema)