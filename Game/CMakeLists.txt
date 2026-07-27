# Outpost - the Eisenfront engine's vertical slice. Not the game itself;
# see Docs/ for what this proves. Every module it needs is linked
# explicitly here rather than relying on transitive PUBLIC deps, so this
# file stays an accurate map of what the slice actually touches.
add_executable(outpost
    src/main.c
    src/ai_soldier.c
    src/game_audio.c
    src/ground_texture.c
    src/outpost_scene.c
    src/player.c
    src/primitives.c
    src/screenshot.c
    src/sky.c
)

target_link_libraries(outpost PRIVATE
    Eisenfront::Core
    Eisenfront::Window
    Eisenfront::Input
    Eisenfront::Graphics
    Eisenfront::Renderer
    Eisenfront::Shader
    Eisenfront::Buffers
    Eisenfront::Texture
    Eisenfront::Mesh
    Eisenfront::Camera
    Eisenfront::Material
    Eisenfront::Scene
    Eisenfront::Ecs
    Eisenfront::RendererFx
    Eisenfront::Physics
    Eisenfront::Audio
    Eisenfront::StbImage
    Eisenfront::StbImageWrite
)

target_compile_features(outpost PRIVATE c_std_23)

target_compile_options(outpost PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic -Wshadow -Werror>
    $<$<C_COMPILER_ID:MSVC>:/W4 /WX>
)

target_compile_definitions(outpost PRIVATE
    OUTPOST_ASSETS_DIR="${CMAKE_SOURCE_DIR}/Assets"
)

# Only in a build that actually has the Editor module (never Release -
# see editor.h's file header comment). The Input/Editor raw-event
# conflict flagged since Phase 1 is resolved in main.c via
# input_process_raw_event()/editor_process_raw_event() chaining.
if(EISENFRONT_BUILD_EDITOR)
    target_sources(outpost PRIVATE src/debug_overlay.c)
    target_link_libraries(outpost PRIVATE Eisenfront::Editor)
    target_compile_definitions(outpost PRIVATE OUTPOST_HAS_EDITOR=1)
endif()
