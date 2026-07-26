add_executable(platform_tests platform_tests/test_platform.c)
target_link_libraries(platform_tests PRIVATE Eisenfront::Platform unity)
add_test(NAME platform_tests COMMAND platform_tests)

add_executable(core_tests core_tests/test_core.c)
target_link_libraries(core_tests PRIVATE Eisenfront::Core unity)
add_test(NAME core_tests COMMAND core_tests)

add_executable(window_tests window_tests/test_window.c)
target_link_libraries(window_tests PRIVATE Eisenfront::Window unity)
add_test(NAME window_tests COMMAND window_tests)
# SDL3's offscreen video driver needs no real display, X server, or GPU
# (exactly what headless CI and this sandbox have) and, unlike "dummy",
# still honors SDL_WINDOW_OPENGL, which Window always requests.
set_tests_properties(window_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")

add_executable(input_tests input_tests/test_input.c)
target_link_libraries(input_tests PRIVATE Eisenfront::Input SDL3::SDL3 unity)
add_test(NAME input_tests COMMAND input_tests)
set_tests_properties(input_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")

add_executable(graphics_tests graphics_tests/test_graphics.c)
target_link_libraries(graphics_tests PRIVATE Eisenfront::Graphics unity)
add_test(NAME graphics_tests COMMAND graphics_tests)
set_tests_properties(graphics_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")

add_executable(renderer_tests renderer_tests/test_renderer.c)
target_link_libraries(renderer_tests PRIVATE Eisenfront::Renderer unity)
add_test(NAME renderer_tests COMMAND renderer_tests)
set_tests_properties(renderer_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")

add_executable(shader_tests shader_tests/test_shader.c)
target_link_libraries(shader_tests PRIVATE Eisenfront::Shader Eisenfront::Graphics unity)
target_compile_definitions(shader_tests PRIVATE
    SHADER_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shader_tests/shaders"
)
add_test(NAME shader_tests COMMAND shader_tests)
set_tests_properties(shader_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")

add_executable(buffers_tests buffers_tests/test_buffers.c)
target_link_libraries(buffers_tests PRIVATE Eisenfront::Buffers unity)
add_test(NAME buffers_tests COMMAND buffers_tests)
set_tests_properties(buffers_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")

add_executable(texture_tests texture_tests/test_texture.c)
target_link_libraries(texture_tests PRIVATE Eisenfront::Texture unity)
target_compile_definitions(texture_tests PRIVATE
    TEXTURE_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/texture_tests/images"
)
add_test(NAME texture_tests COMMAND texture_tests)
set_tests_properties(texture_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")

add_executable(mesh_tests mesh_tests/test_mesh.c)
target_link_libraries(mesh_tests PRIVATE Eisenfront::Mesh unity)
add_test(NAME mesh_tests COMMAND mesh_tests)
set_tests_properties(mesh_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")
