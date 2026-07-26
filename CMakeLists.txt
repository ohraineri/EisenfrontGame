cmake_minimum_required(VERSION 3.25)
project(Eisenfront LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

option(EISENFRONT_BUILD_TESTS "Build Eisenfront unit tests" ON)

add_subdirectory(engine)

if(EISENFRONT_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        unity
        GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
        GIT_TAG v2.6.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(unity)

    enable_testing()
    add_subdirectory(tests)
endif()
