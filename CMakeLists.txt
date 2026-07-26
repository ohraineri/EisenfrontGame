cmake_minimum_required(VERSION 3.25)
project(Eisenfront LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

option(EISENFRONT_BUILD_TESTS "Build Eisenfront unit tests" ON)

include(FetchContent)

# --- SDL3 -------------------------------------------------------------
# Only Window, Input and Graphics Context link SDL3 (see the third-party
# table in CLAUDE.md); Platform and Core never do. Prefer a system SDL3
# install; fall back to fetching and building it from source.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)

find_package(SDL3 QUIET CONFIG)
if(NOT SDL3_FOUND)
    message(STATUS "Eisenfront: system SDL3 not found, fetching release-3.2.30")
    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-3.2.30
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(SDL3)
endif()

add_subdirectory(engine)

if(EISENFRONT_BUILD_TESTS)
    FetchContent_Declare(
        unity
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
        GIT_TAG v2.6.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(unity)
    # PUBLIC: Unity's double-precision assertion support is a compile-time
    # choice baked into libunity itself, not just call sites, so every
    # consumer must agree with it.
    target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE)

    enable_testing()
    add_subdirectory(tests)
endif()
