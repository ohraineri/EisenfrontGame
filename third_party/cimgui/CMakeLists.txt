# Vendored cimgui (MIT, https://github.com/cimgui/cimgui) plus the Dear
# ImGui (MIT, https://github.com/ocornut/imgui) it wraps, used by the
# Editor module for its debug UI.
#
# This is the one deliberate, narrow exception to "this engine is ISO
# C23" (see the file header comment in editor.h): Dear ImGui itself is
# C++, and cimgui.h/cimgui_impl.h are its `extern "C"` ABI boundary.
# Everything below this line is compiled as C++; everything above it
# (this entire target, addressed only through cimgui.h's plain C
# function declarations) is the whole of that boundary - Editor's own
# source is ordinary C23 like every other module.
enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(cimgui STATIC
    cimgui.cpp
    cimgui_impl.cpp
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/backends/imgui_impl_sdl3.cpp
    imgui/backends/imgui_impl_opengl3.cpp
)
add_library(Eisenfront::Cimgui ALIAS cimgui)

target_include_directories(cimgui
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/imgui
            ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)

target_compile_definitions(cimgui PUBLIC
    CIMGUI_USE_SDL3
    CIMGUI_USE_OPENGL3
    IMGUI_IMPL_API=extern\ \"C\"
)
# The OpenGL3 backend's own gl3w-based loader would redeclare every GL
# symbol GLAD's own generated header already declares (see
# third_party/glad); IMGUI_IMPL_OPENGL_LOADER_CUSTOM skips it, and
# force-including glad.h ahead of the backend's translation unit is
# what supplies those declarations instead.
target_compile_definitions(cimgui PRIVATE IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
target_compile_options(cimgui PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:$<$<CXX_COMPILER_ID:GNU,Clang>:-include glad/glad.h>>
)

target_link_libraries(cimgui PUBLIC Eisenfront::Glad)
target_link_libraries(cimgui PRIVATE SDL3::SDL3)
