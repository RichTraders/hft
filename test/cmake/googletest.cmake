include(FetchContent)

FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Disable character conversion warnings for Google Test (LLVM 21+ only)
# -Wcharacter-conversion was added in Clang 21
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "21.0")
        foreach(target gtest gtest_main gmock gmock_main)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE -Wno-character-conversion)
            endif()
        endforeach()
    endif()
endif()