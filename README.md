# Eisenfront

Eisenfront is a custom ISO C23 game engine project designed as the technical foundation for a large-scale first-person multiplayer tactical game. The repository is engine-first: 20 engine modules (Core, Platform, Window, Input, Graphics Context, Renderer, Shader, Buffers, Texture, Mesh, Asset Manager, Camera, Material, Scene, ECS, Renderer FX, Physics, Audio, Networking, Editor), each with its own automated test suite, plus **Outpost** - a small playable vertical slice proving those modules actually work together (see below).

## Documentation

- [Project orientation](docs/00-project-orientation.md)
- [Project overview](docs/01-overview.md)
- [Architecture and diagrams](docs/02-architecture.md)
- [Build, run, and tests](docs/03-build-and-tests.md)
- [Module guide](docs/04-modules.md)
- [Engine usage flows](docs/05-usage-flows.md)
- [Code logic and design reasoning](docs/06-code-logic.md)
- [Current state and roadmap](docs/07-current-state-and-roadmap.md)

## What Already Exists

The repository contains a complete engine foundation:

- Platform layer: time, sleep, files, directories, paths, threads, mutexes, and dynamic libraries.
- Core layer: engine lifecycle, logging, error handling, assertions, frame timing, and module registration.
- Window and input: SDL3 hidden behind engine-owned APIs.
- Graphics: OpenGL 4.6 context, renderer, shaders, buffers, textures, meshes, and PBR materials.
- Renderer FX: framebuffers, lighting data, skybox, directional shadow maps, HDR, bloom, and post-processing.
- World systems: camera, ECS, scene hierarchy, and an in-house C23 physics module (rigid bodies, character controller, raycasts, triggers).
- Assets: path-keyed asset manager with handles, caching, reference counts, and async loading.
- Audio: miniaudio-backed sound loading, spatial/non-spatial playback.
- Networking: ENet-backed reliable/unreliable host and peer wrapper.
- Editor: a Dear ImGui debug overlay, compiled out entirely in Release builds.
- Tests: Unity/CTest tests for every module above (`ctest --test-dir build` - currently 20/20 suites).

## Quick Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DEISENFRONT_BUILD_TESTS=ON
cmake --build build -j
```

## Quick Test

```bash
ctest --test-dir build --output-on-failure
```

If your environment does not provide a real OpenGL 4.6 context, graphics-related tests may fail with `RESULT_ERROR_PLATFORM`. To run only the lower-level tests that do not strongly depend on GL:

```bash
ctest --test-dir build -R 'platform_tests|core_tests|window_tests|input_tests|camera_tests|ecs_tests|scene_tests' --output-on-failure
```

## Outpost - Playable Vertical Slice

`Game/` builds **Outpost**, a small defended forward position (perimeter checkpoint, watchtower, two structures, patrolling AI soldiers) that exists to prove the engine holds together as a whole, not to be the game itself. Every asset is generated in code or synthesized offline (see `Tools/generate_audio.py`) - no external/downloaded art.

Build and run it like any other target in this repo:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/Game/outpost
```

Controls: `WASD` to move, mouse to look, `Space` to jump, `Shift` to sprint, `Esc` to quit. In a non-Release build, `F1` opens a debug overlay (live renderer stats, object counts, a wireframe toggle, noclip, and teleport-to-cursor).

`EISENFRONT_BUILD_GAME=OFF` skips building it; `EISENFRONT_BUILD_EDITOR=OFF` (forced off automatically for `CMAKE_BUILD_TYPE=Release`) drops the F1 overlay from the binary entirely.

### Headless verification

Outpost also runs under SDL3's `offscreen` driver with no real display, input, or audio device - useful for CI or a sandboxed environment. A few environment variables exist purely for that:

```bash
# Run for a fixed number of frames, then exit cleanly:
SDL_VIDEODRIVER=offscreen OUTPOST_MAX_FRAMES=60 ./build/Game/outpost

# Capture a single screenshot of the final frame:
SDL_VIDEODRIVER=offscreen OUTPOST_MAX_FRAMES=10 OUTPOST_SCREENSHOT_PATH=/tmp/frame.png ./build/Game/outpost

# Scripted flythrough: five vantage points around the outpost, one
# screenshot each, then exit - the tool used to visually verify this
# slice without a human at the keyboard:
SDL_VIDEODRIVER=offscreen OUTPOST_SMOKE_TEST_DIR=/tmp/outpost_flythrough ./build/Game/outpost
```

None of these are read in a normal play session - the window's own close button and Esc still work exactly as expected.

Start with [Project orientation](docs/00-project-orientation.md) if you are new to the codebase.
