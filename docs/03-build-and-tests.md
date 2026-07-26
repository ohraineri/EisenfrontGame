# Build, Run, and Tests

## Requirements

Expected minimum requirements:

- CMake 3.25 or newer.
- C compiler with C23 support.
- OpenGL 4.6 capable environment for the full graphics test suite.
- Git and network access on first configure if SDL3 or Unity must be fetched through CMake `FetchContent`.

Intended compilers:

- GCC.
- Clang.
- MSVC.

## Configure

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DEISENFRONT_BUILD_TESTS=ON
```

The root CMake file:

- Uses C23 without extensions.
- Defaults to `Debug` if no build type is provided.
- Enables tests by default through `EISENFRONT_BUILD_TESTS=ON`.
- Looks for system SDL3 first.
- Fetches SDL3 `release-3.2.30` if SDL3 is not found.
- Fetches Unity `v2.6.0` when tests are enabled.

## Build

```bash
cmake --build build -j
```

## Run All Tests

```bash
ctest --test-dir build --output-on-failure
```

## Run Individual Test Groups

```bash
ctest --test-dir build -R platform_tests --output-on-failure
ctest --test-dir build -R core_tests --output-on-failure
ctest --test-dir build -R window_tests --output-on-failure
ctest --test-dir build -R input_tests --output-on-failure
ctest --test-dir build -R graphics_tests --output-on-failure
ctest --test-dir build -R renderer_tests --output-on-failure
ctest --test-dir build -R shader_tests --output-on-failure
ctest --test-dir build -R buffers_tests --output-on-failure
ctest --test-dir build -R texture_tests --output-on-failure
ctest --test-dir build -R mesh_tests --output-on-failure
ctest --test-dir build -R camera_tests --output-on-failure
ctest --test-dir build -R renderer_fx_tests --output-on-failure
ctest --test-dir build -R ecs_tests --output-on-failure
ctest --test-dir build -R scene_tests --output-on-failure
ctest --test-dir build -R material_tests --output-on-failure
ctest --test-dir build -R asset_manager_tests --output-on-failure
```

## Tests That Do Not Strongly Depend On OpenGL

On headless machines or systems without a real OpenGL 4.6 context:

```bash
ctest --test-dir build -R 'platform_tests|core_tests|window_tests|input_tests|camera_tests|ecs_tests|scene_tests' --output-on-failure
```

## Test Matrix

| Test | Target | Requires GL | Notes |
|---|---|---:|---|
| `platform_tests` | `Eisenfront::Platform` | No | OS abstraction |
| `core_tests` | `Eisenfront::Core` | No | Engine, logs, modules, timing |
| `window_tests` | `Eisenfront::Window` | Partial | Uses SDL offscreen |
| `input_tests` | `Eisenfront::Input` | Partial | Uses SDL offscreen |
| `graphics_tests` | `Eisenfront::Graphics` | Yes | Requires OpenGL 4.6 context |
| `renderer_tests` | `Eisenfront::Renderer` | Yes | Draw calls |
| `shader_tests` | `Eisenfront::Shader` | Yes | GL shader compilation |
| `buffers_tests` | `Eisenfront::Buffers` | Yes | Buffer objects and VAOs |
| `texture_tests` | `Eisenfront::Texture` | Yes | GL texture uploads |
| `mesh_tests` | `Eisenfront::Mesh` | Yes | Buffers and VAOs |
| `camera_tests` | `Eisenfront::Camera` | No | Math |
| `renderer_fx_tests` | `Eisenfront::RendererFx` | Yes | FBO, skybox, shadow, postprocess |
| `ecs_tests` | `Eisenfront::Ecs` | No | Sparse-set ECS |
| `scene_tests` | `Eisenfront::Scene` | No | Hierarchy and transforms |
| `material_tests` | `Eisenfront::Material` | Yes | Shader, texture, UBO |
| `asset_manager_tests` | `Eisenfront::AssetManager` | Yes | Textures, shaders, meshes, async |

## SDL Offscreen Driver

Several tests set:

```text
SDL_VIDEODRIVER=offscreen
```

This helps avoid requiring a real display server, but it does not guarantee OpenGL 4.6 support. An offscreen SDL window can still fail to provide the required GL context.

## Interpreting `RESULT_ERROR_PLATFORM`

If graphics tests fail with numeric value `14`, that maps to `RESULT_ERROR_PLATFORM`. In graphics tests this usually means context creation, driver negotiation, or GL function loading failed.

## Build Without Tests

```bash
cmake -S . -B build -DEISENFRONT_BUILD_TESTS=OFF
cmake --build build -j
```

## About `build/`

`build/` contains generated files:

- CMake cache.
- Makefiles or Ninja files.
- Static libraries.
- Test executables.
- Dependencies fetched by CMake.

Do not treat `build/` as source.

