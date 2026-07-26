add_executable(platform_tests platform_tests/test_platform.c)
target_link_libraries(platform_tests PRIVATE Eisenfront::Platform unity)
add_test(NAME platform_tests COMMAND platform_tests)

add_executable(core_tests core_tests/test_core.c)
target_link_libraries(core_tests PRIVATE Eisenfront::Core unity)
add_test(NAME core_tests COMMAND core_tests)

add_executable(physics_tests physics_tests/test_physics.c)
target_link_libraries(physics_tests PRIVATE Eisenfront::Physics unity)
add_test(NAME physics_tests COMMAND physics_tests)

add_executable(audio_tests audio_tests/test_audio.c)
target_link_libraries(audio_tests PRIVATE Eisenfront::Audio unity)
target_compile_definitions(audio_tests PRIVATE
    AUDIO_TEST_SOUND_DIR="${CMAKE_CURRENT_SOURCE_DIR}/audio_tests/sounds"
)
add_test(NAME audio_tests COMMAND audio_tests)

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

add_executable(camera_tests camera_tests/test_camera.c)
target_link_libraries(camera_tests PRIVATE Eisenfront::Camera unity)
add_test(NAME camera_tests COMMAND camera_tests)

add_executable(renderer_fx_tests renderer_fx_tests/test_renderer_fx.c)
target_link_libraries(renderer_fx_tests PRIVATE Eisenfront::RendererFx unity)
target_compile_definitions(renderer_fx_tests PRIVATE
    RENDERER_FX_TEST_TEXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/texture_tests/images"
)
add_test(NAME renderer_fx_tests COMMAND renderer_fx_tests)
set_tests_properties(renderer_fx_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")

add_executable(ecs_tests ecs_tests/test_ecs.c)
target_link_libraries(ecs_tests PRIVATE Eisenfront::Ecs unity)
add_test(NAME ecs_tests COMMAND ecs_tests)

add_executable(scene_tests scene_tests/test_scene.c)
target_link_libraries(scene_tests PRIVATE Eisenfront::Scene unity)
add_test(NAME scene_tests COMMAND scene_tests)

add_executable(material_tests material_tests/test_material.c)
target_link_libraries(material_tests PRIVATE Eisenfront::Material unity)
target_compile_definitions(material_tests PRIVATE
    MATERIAL_TEST_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shader_tests/shaders"
    MATERIAL_TEST_TEXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/texture_tests/images"
)
add_test(NAME material_tests COMMAND material_tests)
set_tests_properties(material_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")

add_executable(asset_manager_tests asset_manager_tests/test_asset_manager.c)
target_link_libraries(asset_manager_tests PRIVATE Eisenfront::AssetManager unity)
target_compile_definitions(asset_manager_tests PRIVATE
    ASSET_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/asset_manager_tests/data"
    ASSET_TEST_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shader_tests/shaders"
    ASSET_TEST_TEXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/texture_tests/images"
)
add_test(NAME asset_manager_tests COMMAND asset_manager_tests)
set_tests_properties(asset_manager_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")
