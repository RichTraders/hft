include(FetchContent)

FetchContent_Declare(
        glaze
        GIT_REPOSITORY https://github.com/stephenberry/glaze
        GIT_TAG        v4.3.0
        GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(glaze)