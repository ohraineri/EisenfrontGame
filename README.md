# Eisenfront

Eisenfront is a custom ISO C23 game engine project designed as the technical foundation for a large-scale first-person multiplayer tactical game. The repository is engine-first: it currently provides modular static libraries, public APIs, and automated tests, but it does not yet contain a playable game executable, editor, or official demo application.

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

The repository already contains a substantial engine foundation:

- Platform layer: time, sleep, files, directories, paths, threads, mutexes, and dynamic libraries.
- Core layer: engine lifecycle, logging, error handling, assertions, frame timing, and module registration.
- Window and input: SDL3 hidden behind engine-owned APIs.
- Graphics: OpenGL 4.6 context, renderer, shaders, buffers, textures, meshes, and PBR materials.
- Renderer FX: framebuffers, lighting data, skybox, directional shadow maps, HDR, bloom, and post-processing.
- World systems: camera, ECS, scene hierarchy, and a physics module present in source.
- Assets: path-keyed asset manager with handles, caching, reference counts, and partial async loading.
- Tests: Unity/CTest tests for the main engine modules.

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

## Important Status Note

This repository is currently an engine foundation, not a complete game. There is no `game/` target, launcher, editor, or interactive demo yet. The `physics` module exists in `engine/include` and `engine/src`, but in the currently observed CMake setup it is not connected to the main engine build.

Start with [Project orientation](docs/00-project-orientation.md) if you are new to the codebase.
