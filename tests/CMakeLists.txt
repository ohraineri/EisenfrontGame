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

add_executable(networking_tests networking_tests/test_networking.c)
target_link_libraries(networking_tests PRIVATE Eisenfront::Networking unity)
add_test(NAME networking_tests COMMAND networking_tests)

# Compiles the Game-side .c directly by path (Game builds an executable,
# not a library, so there's nothing to link against here).
add_executable(player_movement_tests
    player_movement_tests/test_player_movement.c
    ${CMAKE_SOURCE_DIR}/Game/src/player_movement.c
)
target_include_directories(player_movement_tests PRIVATE ${CMAKE_SOURCE_DIR}/Game/src)
target_link_libraries(player_movement_tests PRIVATE Eisenfront::Cglm unity)
if(NOT WIN32)
    target_link_libraries(player_movement_tests PRIVATE m)
endif()
add_test(NAME player_movement_tests COMMAND player_movement_tests)

# Pure - no engine dependency at all.
add_executable(sim_clock_tests
    sim_clock_tests/test_sim_clock.c
    ${CMAKE_SOURCE_DIR}/Game/src/sim_clock.c
)
target_include_directories(sim_clock_tests PRIVATE ${CMAKE_SOURCE_DIR}/Game/src)
target_link_libraries(sim_clock_tests PRIVATE unity)
add_test(NAME sim_clock_tests COMMAND sim_clock_tests)

# Pure - camera_motion.c depends only on player_movement.c/cglm, plus
# sim_clock.c for the frame-rate-equivalence test's driver loop.
add_executable(camera_motion_tests
    camera_motion_tests/test_camera_motion.c
    ${CMAKE_SOURCE_DIR}/Game/src/camera_motion.c
    ${CMAKE_SOURCE_DIR}/Game/src/player_movement.c
    ${CMAKE_SOURCE_DIR}/Game/src/sim_clock.c
)
target_include_directories(camera_motion_tests PRIVATE ${CMAKE_SOURCE_DIR}/Game/src)
target_link_libraries(camera_motion_tests PRIVATE Eisenfront::Cglm unity)
if(NOT WIN32)
    target_link_libraries(camera_motion_tests PRIVATE m)
endif()
add_test(NAME camera_motion_tests COMMAND camera_motion_tests)

# Only the pure axis-resolve function is exercised (see the test file's
# header comment); the object file still references Input symbols from
# infantry_input_command_sample(), so Input/SDL3 must link even though
# no test calls it.
add_executable(infantry_input_command_tests
    infantry_input_command_tests/test_infantry_input_command.c
    ${CMAKE_SOURCE_DIR}/Game/src/infantry_input_command.c
)
target_include_directories(infantry_input_command_tests PRIVATE ${CMAKE_SOURCE_DIR}/Game/src)
target_link_libraries(infantry_input_command_tests PRIVATE Eisenfront::Input SDL3::SDL3 unity)
add_test(NAME infantry_input_command_tests COMMAND infantry_input_command_tests)

# Integration-level: exercises infantry_entity.c (and the modules it
# composes: player_movement.c, infantry_input_command.c) against a real
# PhysicsWorld with real floor/ceiling geometry - the crouch/jump
# interaction scenarios (a blocked stand-up, an actual landing) need
# real collision, not just synthetic vectors.
add_executable(infantry_entity_tests
    infantry_entity_tests/test_infantry_entity.c
    ${CMAKE_SOURCE_DIR}/Game/src/infantry_entity.c
    ${CMAKE_SOURCE_DIR}/Game/src/player_movement.c
    ${CMAKE_SOURCE_DIR}/Game/src/infantry_input_command.c
    ${CMAKE_SOURCE_DIR}/Game/src/camera_motion.c
)
target_include_directories(infantry_entity_tests PRIVATE ${CMAKE_SOURCE_DIR}/Game/src)
target_link_libraries(infantry_entity_tests PRIVATE
    Eisenfront::Physics Eisenfront::Camera Eisenfront::Ecs Eisenfront::Input SDL3::SDL3 unity
)
if(NOT WIN32)
    target_link_libraries(infantry_entity_tests PRIVATE m)
endif()
add_test(NAME infantry_entity_tests COMMAND infantry_entity_tests)

if(EISENFRONT_BUILD_EDITOR)
    add_executable(editor_tests editor_tests/test_editor.c)
    target_link_libraries(editor_tests PRIVATE Eisenfront::Editor SDL3::SDL3 unity)
    add_test(NAME editor_tests COMMAND editor_tests)
    set_tests_properties(editor_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")
endif()

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

# Integration-level: builds the real Outpost level (real shader,
# materials, and PhysicsWorld bodies) and checks that
# outpost_level_surface_type_for_body() actually agrees with what
# outpost_scene.c tagged each object with at each add_box() call site.
add_executable(outpost_scene_tests
    outpost_scene_tests/test_outpost_scene.c
    ${CMAKE_SOURCE_DIR}/Game/src/outpost_scene.c
    ${CMAKE_SOURCE_DIR}/Game/src/primitives.c
    ${CMAKE_SOURCE_DIR}/Game/src/surface_type.c
)
target_include_directories(outpost_scene_tests PRIVATE ${CMAKE_SOURCE_DIR}/Game/src)
target_link_libraries(outpost_scene_tests PRIVATE
    Eisenfront::Physics Eisenfront::Scene Eisenfront::Material Eisenfront::Mesh Eisenfront::Cglm unity
)
target_compile_definitions(outpost_scene_tests PRIVATE
    OUTPOST_SCENE_TEST_SHADER_DIR="${CMAKE_SOURCE_DIR}/Assets/shaders"
)
add_test(NAME outpost_scene_tests COMMAND outpost_scene_tests)
set_tests_properties(outpost_scene_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")

# Real miniaudio engine, real per-surface .wav files from Assets/audio -
# a missing/misnamed footstep_<surface>.wav fails here exactly like it
# would at game startup.
add_executable(game_audio_tests
    game_audio_tests/test_game_audio.c
    ${CMAKE_SOURCE_DIR}/Game/src/game_audio.c
    ${CMAKE_SOURCE_DIR}/Game/src/surface_type.c
)
target_include_directories(game_audio_tests PRIVATE ${CMAKE_SOURCE_DIR}/Game/src)
target_link_libraries(game_audio_tests PRIVATE Eisenfront::Audio unity)
target_compile_definitions(game_audio_tests PRIVATE
    GAME_AUDIO_TEST_ASSETS_DIR="${CMAKE_SOURCE_DIR}/Assets"
)
add_test(NAME game_audio_tests COMMAND game_audio_tests)

add_executable(asset_manager_tests asset_manager_tests/test_asset_manager.c)
target_link_libraries(asset_manager_tests PRIVATE Eisenfront::AssetManager unity)
target_compile_definitions(asset_manager_tests PRIVATE
    ASSET_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/asset_manager_tests/data"
    ASSET_TEST_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shader_tests/shaders"
    ASSET_TEST_TEXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/texture_tests/images"
)
add_test(NAME asset_manager_tests COMMAND asset_manager_tests)
set_tests_properties(asset_manager_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen")
