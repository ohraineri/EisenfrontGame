# Project Orientation

This document is the fastest way to become comfortable in the Eisenfront codebase. It explains what to read first, how the project thinks about layers, and how to make your first changes without getting lost.

## The Mental Model

Eisenfront is not structured like a small game prototype where gameplay, rendering, input, and file loading all call each other freely. It is structured like an engine: each module owns one responsibility, exposes a small public API, and depends only on lower layers.

The most important rule is:

```text
Game code should depend on Engine APIs.
Engine modules should depend downward.
Platform-specific details stay at the bottom.
SDL and OpenGL should not leak into gameplay.
```

## First Files To Read

Read in this order:

1. `README.md`
2. `docs/00-project-orientation.md`
3. `docs/01-overview.md`
4. `docs/02-architecture.md`
5. `engine/CMakeLists.txt`
6. `engine/include/eisenfront/core.h`
7. `engine/include/eisenfront/platform.h`
8. The header for the module you want to work on.
9. The matching implementation file in `engine/src`.
10. The matching test file in `tests`.

The public header usually explains the module's contract better than the `.c` file does. Start with the contract, then read the implementation.

## Directory Map

```text
.
├── CMakeLists.txt                 Root build file and dependency setup
├── README.md                      Short entry point
├── CLAUDE.md                      Engineering rules and project intent
├── docs/                          External documentation
├── engine/
│   ├── CMakeLists.txt             Engine library targets
│   ├── include/eisenfront/        Public engine APIs
│   └── src/                       Module implementations
├── tests/                         Unity/CTest module tests
├── third_party/                   Vendored or locally wrapped dependencies
└── build/                         Generated build output
```

## How To Recognize A Module

A module usually has:

- One public header in `engine/include/eisenfront`.
- One or more implementation files in `engine/src/<module>`.
- One static library target in `engine/CMakeLists.txt`.
- One test executable in `tests/<module>_tests`.
- A CMake alias such as `Eisenfront::Core`.

Example:

```text
engine/include/eisenfront/texture.h
engine/src/texture/texture.c
tests/texture_tests/test_texture.c
Eisenfront::Texture
```

## Common Workflows

### Working On A Core Module

1. Read the header.
2. Read its tests.
3. Read the implementation.
4. Make the smallest change that preserves the API contract.
5. Run that module's test target.
6. Run nearby tests if the module is shared.

### Adding A New Engine Module

1. Add a public header under `engine/include/eisenfront`.
2. Add implementation under `engine/src/<module>`.
3. Add a static library in `engine/CMakeLists.txt`.
4. Add an alias target such as `Eisenfront::<Module>`.
5. Add tests under `tests/<module>_tests`.
6. Register the test executable in `tests/CMakeLists.txt`.
7. Update external docs.

### Debugging A Test Failure

First classify the test:

- Pure engine test: Platform, Core, Camera, ECS, Scene.
- SDL/offscreen test: Window, Input.
- OpenGL test: Graphics, Renderer, Shader, Buffers, Texture, Mesh, Material, Renderer FX, Asset Manager.

If the failure is `RESULT_ERROR_PLATFORM` in a GL test, confirm the machine can create the required OpenGL 4.6 context before assuming the module logic is wrong.

## Naming Conventions

- Files and functions use `snake_case`.
- Structs and enums use `PascalCase`.
- Macros and enum values use `UPPER_CASE`.
- Public functions are prefixed with the module name: `renderer_create`, `texture_load_2d`, `scene_node_create`.
- Fallible functions return `Result`.
- Owned opaque objects use explicit create/destroy or retain/release pairs.

## What To Avoid

- Do not add SDL types to gameplay-facing APIs.
- Do not add OS headers above the Platform layer.
- Do not add C++.
- Do not make gameplay call raw OpenGL directly.
- Do not hide ownership transfers.
- Do not add new global mutable state unless the module already has a clearly documented process-wide system.
- Do not treat `build/` as source.

## Where To Start If You Want To Build A Demo

A minimal demo would likely need:

- `examples/` or `game/` directory.
- Window creation.
- Graphics context creation.
- Shader, texture, material, and renderer initialization.
- A mesh, a shader, and a camera.
- A frame loop with input and renderer submission.

The existing code already has the lower-level pieces. What is missing is the executable that wires them together.

