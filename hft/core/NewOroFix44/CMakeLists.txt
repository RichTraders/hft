set(FIX8_SRC
        NewOroFix44MD_types.hpp
        NewOroFix44MD_types.cpp
        NewOroFix44MD_traits.cpp
        NewOroFix44MD_router.hpp
        NewOroFix44MD_classes.hpp
        NewOroFix44MD_classes.cpp

        NewOroFix44OE_types.hpp
        NewOroFix44OE_types.cpp
        NewOroFix44OE_traits.cpp
        NewOroFix44OE_router.hpp
        NewOroFix44OE_classes.hpp
        NewOroFix44OE_classes.cpp
)

add_library(Fix8Schema STATIC ${FIX8_SRC})
target_precompile_headers(Fix8Schema PUBLIC ${CMAKE_SOURCE_DIR}/include/pch.h)
target_include_directories(Fix8Schema SYSTEM PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        /usr/local/include
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/hft
)
target_compile_options(Fix8Schema PRIVATE
        -Wno-missing-field-initializers
        -Wno-unused-parameter
        -Wno-error=unused-parameter
        -Wno-return-type-c-linkage
        -Wno-error=return-type-c-linkage
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