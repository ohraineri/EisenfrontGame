# Outpost - the Eisenfront engine's vertical slice. Not the game itself;
# see Docs/ for what this proves. Every module it needs is linked
# explicitly here rather than relying on transitive PUBLIC deps, so this
# file stays an accurate map of what the slice actually touches.
add_executable(outpost
    src/main.c
    src/ai_soldier.c
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

# Eisenfront::Editor is linked starting in Phase 8, once the
# Input/Editor raw-event-callback conflict (see main.c) is resolved at
# the engine level - see the plan this vertical slice was built from.
