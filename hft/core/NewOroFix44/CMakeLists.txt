set(FIX8_SRC
        NewOroFix44_types.hpp
        NewOroFix44_types.cpp
        NewOroFix44_traits.cpp
        NewOroFix44_router.hpp
        NewOroFix44_classes.hpp
        NewOroFix44_classes.cpp
        signature.cpp
        ssl_socket.cpp
        fix_wrapper.cpp
        fix_app.cpp
)

add_library(Fix8Schema STATIC ${FIX8_SRC})
target_precompile_headers(Fix8Schema PUBLIC ${CMAKE_SOURCE_DIR}/include/pch.h)
target_include_directories(Fix8Schema PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        /usr/local/include
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/hft
)
target_link_directories(Fix8Schema PUBLIC /usr/local/lib)
target_link_libraries(Fix8Schema PUBLIC
        fix8
        Common
        z
        PocoFoundation
        PocoNet
        stdc++
        gcc_s
        pcre
        m
        ssl
        crypto
)

target_compile_options(Fix8Schema PRIVATE -Wno-missing-field-initializers -Wno-error=unused-parameter)