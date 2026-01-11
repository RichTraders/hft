file(GLOB CORE_COMMON_SRC
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.tpp"
)

if(USE_FUTURES_API)
  find_package(CURL REQUIRED)
  file(GLOB CORE_HTTP_SRC
      "${CMAKE_CURRENT_SOURCE_DIR}/http/*.cpp"
  )
else()
  set(CORE_HTTP_SRC "")
endif()

set(TARGET_EXCHANGE "binance" CACHE STRING "Target exchange (e.g., binance, bybit)")

if(USE_FUTURES_API)
  set(MARKET_TYPE "futures")
else()
  set(MARKET_TYPE "spot")
endif()

set(EXCHANGE_TRAITS_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/websocket/market_data/exchanges/${TARGET_EXCHANGE}/${MARKET_TYPE}")

if(NOT IS_DIRECTORY "${EXCHANGE_TRAITS_DIR}")
  message(FATAL_ERROR "Exchange traits directory not found: ${EXCHANGE_TRAITS_DIR}")
endif()

string(TOUPPER "${TARGET_EXCHANGE}" UPPER_EXCHANGE)
string(TOUPPER "${MARKET_TYPE}" UPPER_MARKET_TYPE)
set(EXCHANGE_COMPILE_DEFINITION "TARGET_EXCHANGE_${UPPER_EXCHANGE}_${UPPER_MARKET_TYPE}")

if(ENABLE_WEBSOCKET)
  file(GLOB CORE_WS_SRC
      "${CMAKE_CURRENT_SOURCE_DIR}/websocket/*.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/websocket/*.tpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/websocket/market_data/*.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/websocket/market_data/*.hpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/websocket/market_data/*.tpp"

      "${CMAKE_CURRENT_SOURCE_DIR}/websocket/order_entry/*.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/websocket/order_entry/*.hpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/websocket/order_entry/*.hpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/websocket/order_entry/*.tpp"
      ${EXCHANGE_TRAITS_DIR}
  )
  find_package(TBB REQUIRED)
  add_library(Core_ws STATIC ${CORE_COMMON_SRC} ${CORE_WS_SRC} ${CORE_HTTP_SRC})
  target_precompile_headers(Core_ws PUBLIC ${CMAKE_SOURCE_DIR}/include/pch.h)
  target_include_directories(Core_ws PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_SOURCE_DIR}/common
      ${CMAKE_SOURCE_DIR}/include
      ${CMAKE_CURRENT_SOURCE_DIR}/websocket
      ${EXCHANGE_TRAITS_DIR}
  )
  target_link_libraries(Core_ws PUBLIC Common websockets glaze::glaze TBB::tbb)
  if(USE_FUTURES_API)
    target_link_libraries(Core_ws PUBLIC CURL::libcurl)
  endif()
  target_compile_definitions(Core_ws PRIVATE ENABLE_WEBSOCKET ${EXCHANGE_COMPILE_DEFINITION})
endif()

if(NOT ENABLE_WEBSOCKET)
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
endif()