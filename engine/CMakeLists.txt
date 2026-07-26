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

# --- Input -------------------------------------------------------------
# SDL3 is a private implementation detail of input.c; eisenfront/input.h
# never includes an SDL header.
add_library(eisenfront_input STATIC
    src/input/input.c
)
add_library(Eisenfront::Input ALIAS eisenfront_input)

target_include_directories(eisenfront_input
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(eisenfront_input PUBLIC c_std_23)

target_compile_options(eisenfront_input PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
)

target_link_libraries(eisenfront_input PUBLIC Eisenfront::Window)
target_link_libraries(eisenfront_input PRIVATE SDL3::SDL3)

# --- Graphics Context -----------------------------------------------------
# SDL3 stays private (context creation/swap only); GLAD is PUBLIC because
# eisenfront/graphics_context.h intentionally exposes OpenGL to the
# rendering modules built on top of it (see that header's file comment).
add_library(eisenfront_graphics STATIC
    src/graphics/graphics_context.c
)
add_library(Eisenfront::Graphics ALIAS eisenfront_graphics)

target_include_directories(eisenfront_graphics
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(eisenfront_graphics PUBLIC c_std_23)

target_compile_options(eisenfront_graphics PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
)

target_link_libraries(eisenfront_graphics PUBLIC Eisenfront::Window Eisenfront::Glad)
target_link_libraries(eisenfront_graphics PRIVATE SDL3::SDL3)

# --- Renderer -------------------------------------------------------------
add_library(eisenfront_renderer STATIC
    src/renderer/renderer.c
)
add_library(Eisenfront::Renderer ALIAS eisenfront_renderer)

target_include_directories(eisenfront_renderer
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(eisenfront_renderer PUBLIC c_std_23)

target_compile_options(eisenfront_renderer PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
)

target_link_libraries(eisenfront_renderer PUBLIC Eisenfront::Graphics)

# --- Shader -------------------------------------------------------------
add_library(eisenfront_shader STATIC
    src/shader/shader.c
)
add_library(Eisenfront::Shader ALIAS eisenfront_shader)

target_include_directories(eisenfront_shader
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(eisenfront_shader PUBLIC c_std_23)

target_compile_options(eisenfront_shader PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
)

target_link_libraries(eisenfront_shader PUBLIC Eisenfront::Graphics Eisenfront::Platform)

# --- Buffers -------------------------------------------------------------
add_library(eisenfront_buffers STATIC
    src/buffers/buffers.c
)
add_library(Eisenfront::Buffers ALIAS eisenfront_buffers)

target_include_directories(eisenfront_buffers
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(eisenfront_buffers PUBLIC c_std_23)

target_compile_options(eisenfront_buffers PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
)

target_link_libraries(eisenfront_buffers PUBLIC Eisenfront::Graphics)

# --- Texture -------------------------------------------------------------
# stb_image is a private implementation detail of texture.c;
# eisenfront/texture.h never includes it.
add_library(eisenfront_texture STATIC
    src/texture/texture.c
)
add_library(Eisenfront::Texture ALIAS eisenfront_texture)

target_include_directories(eisenfront_texture
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(eisenfront_texture PUBLIC c_std_23)

target_compile_options(eisenfront_texture PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
)

target_link_libraries(eisenfront_texture PUBLIC Eisenfront::Graphics Eisenfront::Platform)
target_link_libraries(eisenfront_texture PRIVATE Eisenfront::StbImage)

# --- Mesh -------------------------------------------------------------
add_library(eisenfront_mesh STATIC
    src/mesh/mesh.c
)
add_library(Eisenfront::Mesh ALIAS eisenfront_mesh)

target_include_directories(eisenfront_mesh
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(eisenfront_mesh PUBLIC c_std_23)

target_compile_options(eisenfront_mesh PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
)

target_link_libraries(eisenfront_mesh PUBLIC Eisenfront::Buffers)

# --- Asset Manager -------------------------------------------------------
# cgltf is a private implementation detail of asset_manager.c;
# eisenfront/asset_manager.h never includes it.
add_library(eisenfront_asset_manager STATIC
    src/asset_manager/asset_manager.c
)
add_library(Eisenfront::AssetManager ALIAS eisenfront_asset_manager)

target_include_directories(eisenfront_asset_manager
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(eisenfront_asset_manager PUBLIC c_std_23)

target_compile_options(eisenfront_asset_manager PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
)

target_link_libraries(eisenfront_asset_manager PUBLIC
    Eisenfront::Texture Eisenfront::Shader Eisenfront::Mesh Eisenfront::Platform
)
target_link_libraries(eisenfront_asset_manager PRIVATE Eisenfront::Cgltf)

# --- Camera -------------------------------------------------------------
add_library(eisenfront_camera STATIC
    src/camera/camera.c
)
add_library(Eisenfront::Camera ALIAS eisenfront_camera)

target_include_directories(eisenfront_camera
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(eisenfront_camera PUBLIC c_std_23)

target_compile_options(eisenfront_camera PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
)

target_link_libraries(eisenfront_camera PUBLIC Eisenfront::Core Eisenfront::Cglm)
if(NOT WIN32)
    target_link_libraries(eisenfront_camera PUBLIC m)
endif()
