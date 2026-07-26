# --- Platform -------------------------------------------------------------
# Backend sources are selected per target OS; nothing outside this list
# depends on which backend is active (see eisenfront/platform.h).
set(EISENFRONT_PLATFORM_COMMON_SOURCES
    src/platform/common/result.c
    src/platform/common/path_common.c
)

if(WIN32)
    set(EISENFRONT_PLATFORM_BACKEND_SOURCES
        src/platform/windows/time_windows.c
        src/platform/windows/sleep_windows.c
        src/platform/windows/mutex_windows.c
        src/platform/windows/thread_windows.c
        src/platform/windows/dynlib_windows.c
        src/platform/windows/filesystem_windows.c
        src/platform/windows/directory_windows.c
        src/platform/windows/path_windows.c
    )
else()
    # Linux and macOS share one POSIX backend; see engine/src/platform/posix.
    set(EISENFRONT_PLATFORM_BACKEND_SOURCES
        src/platform/posix/time_posix.c
        src/platform/posix/sleep_posix.c
        src/platform/posix/mutex_posix.c
        src/platform/posix/thread_posix.c
        src/platform/posix/dynlib_posix.c
        src/platform/posix/filesystem_posix.c
        src/platform/posix/directory_posix.c
        src/platform/posix/path_posix.c
    )
endif()

add_library(eisenfront_platform STATIC
    ${EISENFRONT_PLATFORM_COMMON_SOURCES}
    ${EISENFRONT_PLATFORM_BACKEND_SOURCES}
)
add_library(Eisenfront::Platform ALIAS eisenfront_platform)

target_include_directories(eisenfront_platform
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(eisenfront_platform PUBLIC c_std_23)

target_compile_options(eisenfront_platform PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
)

if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(eisenfront_platform PRIVATE Threads::Threads)
    # dlopen/dlsym/dlclose (glibc puts these in libdl pre-2.34, folded into
    # libc from 2.34 onward; linking libdl is a harmless no-op either way).
    target_link_libraries(eisenfront_platform PRIVATE ${CMAKE_DL_LIBS})
endif()
# Windows needs no extra link libraries: kernel32 (threads, sync, files,
# LoadLibrary) is linked by default by every supported toolchain.

# --- Core -------------------------------------------------------------
# Core depends only on Platform (never SDL3 or any other third party
# library directly); see eisenfront/core.h.
add_library(eisenfront_core STATIC
    src/core/core_assert.c
    src/core/core_error.c
    src/core/core_log.c
    src/core/core_stopwatch.c
    src/core/core_config.c
    src/core/core_module.c
    src/core/core_engine.c
)
add_library(Eisenfront::Core ALIAS eisenfront_core)

target_include_directories(eisenfront_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(eisenfront_core PUBLIC c_std_23)

target_compile_options(eisenfront_core PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
)

target_link_libraries(eisenfront_core PUBLIC Eisenfront::Platform)

# --- Window -------------------------------------------------------------
# SDL3 is a private implementation detail of window.c; eisenfront/window.h
# never includes an SDL header (see that header's file comment).
add_library(eisenfront_window STATIC
    src/window/window.c
)
add_library(Eisenfront::Window ALIAS eisenfront_window)

target_include_directories(eisenfront_window
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(eisenfront_window PUBLIC c_std_23)

target_compile_options(eisenfront_window PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
)

target_link_libraries(eisenfront_window PUBLIC Eisenfront::Core)
target_link_libraries(eisenfront_window PRIVATE SDL3::SDL3)
