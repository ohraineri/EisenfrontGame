# Current State And Roadmap

## Current State

The project already has a promising engine architecture with many separated modules, public APIs, and module-level tests. The strongest areas are:

- Separation of responsibilities.
- SDL encapsulation.
- Explicit ownership contracts.
- Consistent `Result` usage.
- CMake targets per module.
- Isolated tests.
- Early support for async assets, material instances, scene management, and ECS.

## What Is Implemented

| Area | State |
|---|---|
| Platform | Implemented and connected to build |
| Core | Implemented and connected to build |
| Window | Implemented and connected to build |
| Input | Implemented and connected to build |
| Graphics Context | Implemented and connected to build |
| Renderer | Implemented and connected to build |
| Shader | Implemented and connected to build |
| Buffers | Implemented and connected to build |
| Texture | Implemented and connected to build |
| Mesh | Implemented and connected to build |
| Material | Implemented and connected to build |
| Asset Manager | Implemented and connected to build |
| Camera | Implemented and connected to build |
| ECS | Implemented and connected to build |
| Scene | Implemented and connected to build |
| Renderer FX | Implemented and connected to build |
| Physics | Source present, apparently not connected to the main CMake build |

## Product Gaps

- No game executable.
- No official demo.
- No editor.
- No runtime asset path/configuration system.
- No versioned application main loop.
- No complete integration of Scene, ECS, Renderer, and Assets.

## Engine Gaps

- `Physics` should be connected to the build if it is official engine scope.
- `physics_tests` exists as a directory, but it is not registered in the observed `tests/CMakeLists.txt`.
- Material is not yet an Asset Manager asset type.
- glTF import does not yet cover full materials, textures, skinning, and animations.
- Base Renderer still operates on raw GL handles.
- There is no complete default render path with camera UBO, lit material shader, and scene renderer.
- There is no audio module.
- There is no networking module.
- There is no UI module.
- There is no virtual filesystem or asset database.
- There is no global coordinated hot reload for shaders, textures, models, and scenes.

## Technical Risks

| Risk | Impact | Mitigation |
|---|---|---|
| GL tests are environment-sensitive | CI/local failures without the right driver | Separate non-GL tests from tests requiring real GL |
| Fixed-capacity systems | Simple but may limit scale | Document capacity and make it configurable per app |
| Renderer uses raw GL handles | Flexible but less type-safe | Add a higher-level render layer above the command API |
| Asset Manager delegates cache to Texture/Shader | Ownership rules can become subtle | Keep strong retain/release/reload tests |
| Physics outside build | Source can decay silently | Connect it to CMake and tests |
| Assertions during cleanup paths | Tests may abort instead of returning clear failures | Review release/destroy behavior after partial initialization failure |

## Suggested Roadmap

### Phase 1: Stabilization

- Connect `physics` to CMake or explicitly remove it from official scope for now.
- Register `physics_tests` if valid tests already exist.
- Fix cleanup paths that assert after earlier initialization failures.
- Clearly separate tests that require real OpenGL.
- Create a minimal `eisenfront_demo` target.

### Phase 2: Application Runtime

- Create `game/` or `examples/`.
- Implement an official main loop.
- Initialize Window, Graphics, Input, Renderer, Texture, Shader, Material, and Asset Manager together.
- Render a minimal scene with camera, mesh, shader, and texture.
- Add runtime configuration for resolution, VSync, and asset paths.

### Phase 3: Higher-Level Render Path

- Add a per-frame/per-camera UBO.
- Add a standard lit shader.
- Integrate Material, Lighting, and Shadow into a demonstrable flow.
- Add a scene renderer that walks Scene/ECS and emits RenderCommands.
- Add debug views for shadow maps, normals, and framebuffers.

### Phase 4: Assets And Content

- Improve glTF import.
- Import materials and textures.
- Define a scene file format.
- Add an asset manifest.
- Add coordinated hot reload.

### Phase 5: Game Features

- Add gameplay entities.
- Add player controller.
- Add weapons and interaction.
- Add networking.
- Add audio.
- Add UI/HUD.
- Add debug tooling.

## Test State Observed In This Session

In this local environment, 7 of 16 tests passed and 9 failed. The main failures happened in OpenGL-dependent modules with `RESULT_ERROR_PLATFORM`, which points to context, driver, or platform limitations in the current environment.

A `SIGTRAP` was also observed in `material_tests`, caused by `shader_program_release()` receiving `nullptr` during cleanup after an earlier failure. That is a robustness issue worth investigating.

## Definition Of Ready For The Next Stage

A good "ready for demo work" state would be:

- Clean build on Linux.
- Non-GL tests passing consistently.
- GL tests documented and passing in a known OpenGL 4.6 environment.
- Physics either integrated or explicitly out of scope.
- Minimal executable demo.
- README pointing to build, tests, and demo.
- Runtime architecture documented and exercised by code.

