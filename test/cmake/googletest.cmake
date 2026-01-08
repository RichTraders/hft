include(FetchContent)

FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Disable character conversion warnings for Google Test
if(TARGET gtest)
    target_compile_options(gtest PRIVATE -Wno-character-conversion)
endif()
if(TARGET gtest_main)
    target_compile_options(gtest_main PRIVATE -Wno-character-conversion)
endif()
if(TARGET gmock)
    target_compile_options(gmock PRIVATE -Wno-character-conversion)
endif()
if(TARGET gmock_main)
    target_compile_options(gmock_main PRIVATE -Wno-character-conversion)
endif()