cmake_minimum_required(VERSION 3.25)
project(Eisenfront LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

option(EISENFRONT_BUILD_TESTS "Build Eisenfront smoke tests" ON)

# --- SDL3 -------------------------------------------------------------
# Foundation depends on SDL3 only for its monotonic performance counter and
# mutex primitives (see engine/src/foundation/ef_timer.c, ef_log.c); it
# never touches SDL3's video/audio/render subsystems. Prefer a system
# SDL3 install; fall back to fetching and building it from source.
include(FetchContent)
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
    enable_testing()
    add_subdirectory(tests)
endif()
