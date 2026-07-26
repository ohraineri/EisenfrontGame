# CLAUDE.md

## Project Overview

Custom cross-platform game engine written in **ISO C23**, built to power a large-scale first-person multiplayer tactical game.

This is an **engine project first, game project second**. Architecture and long-term maintainability always outrank implementation speed. Write every piece of code as if it will be maintained for the next ten years — simple, readable, modular, explicit, portable. Avoid clever code; prefer boring, understandable solutions.

---

## Language Rules

**Only standard ISO C23.** Never generate C++. Never use: classes, templates, namespaces, exceptions, operator overloading, STL, RAII, smart pointers, lambdas, constructors/destructors, or C++-style casts.

## Platforms & Build

- Targets: Linux, Windows, macOS. Platform-specific code stays isolated; the majority of the engine is platform-independent.
- Build system: CMake. Every module compiles independently, with no hidden dependencies. Must build with GCC, Clang, and MSVC.

## Third-Party Libraries

| Purpose | Library |
|---|---|
| Window / Input | SDL3 |
| Rendering | OpenGL 4.6 |
| GL Loader | GLAD |
| Math | cglm |
| Model Loading | cgltf |
| Image Loading | stb_image |
| Image Writing | stb_image_write |
| Audio | miniaudio |
| Networking | ENet |
| Immediate Mode UI | cimgui |
| Memory Allocator (optional) | mimalloc |
| Testing | Unity Test Framework for C |

---

## Architecture

```
Engine/
  Core, Platform, Window, Input, Graphics, Renderer,
  Assets, Math, Audio, Physics, Networking, ECS,
  Scene, UI, Memory, FileSystem, Utilities
Game/
Assets/
Tools/
Tests/
Docs/
```

**Dependency direction is one-way:** `Game → Engine → Platform`, never the reverse.
- Gameplay code never calls SDL or OpenGL directly — only Engine APIs.
- Each module exposes one public header (e.g. `renderer.h` / `renderer.c`); internal helpers are `static`; hide implementation details and avoid exposing structures unnecessarily.
- Never mix responsibilities across modules.

### Subsystem Notes

- **Renderer**: fully independent of gameplay. Gameplay submits render commands; the renderer executes them. Use modern OpenGL only (no deprecated APIs) — VAO/VBO/EBO/UBO/SSBO, framebuffers. Enable debug context + callback; check GL errors in debug builds.
- **Assets**: never load the same asset twice — cache. Reference via handles, not raw pointers, whenever possible. Design for future hot-reload and async loading.
- **ECS**: composition over inheritance. Components hold only data; systems hold behavior; entities are just IDs. Design for cache locality.
- **Scene**: scenes own entities, engine owns scenes. Avoid global state.
- **Threading**: assume the engine will eventually be multithreaded — avoid mutable globals now, document synchronization requirements as they appear.

---

## API & Naming Conventions

| Element | Convention | Example |
|---|---|---|
| Functions, variables, files | `snake_case` | `renderer_init()`, `texture_load()` |
| Macros | `UPPER_CASE` | `MAX_ENTITIES` |
| Structs, enums | `PascalCase` | `WindowMode` |
| Enum values | `UPPER_CASE` | `WINDOW_MODE_FULLSCREEN` |

Avoid generic names (`init()`, `update()`) — always prefix with the module (`renderer_init()`, `camera_update()`).

```c
typedef enum
{
    WINDOW_MODE_WINDOWED,
    WINDOW_MODE_FULLSCREEN
} WindowMode;
```

## Coding Style

- Prefer early returns; avoid deep nesting.
- Keep functions focused and short — ~100 lines is the recommended ceiling; split when exceeded.
- Prefer several small functions over one large one.

## Memory & Error Handling

- Every allocation has exactly one explicit owner and a matching destroy/free function. Document ownership. No leaks.
- Prefer stack allocation where practical; avoid heap allocations and hidden allocations in hot loops.
- Never terminate unexpectedly — return explicit error codes, propagate upward, log meaningfully, never fail silently.

## Logging

Levels: Trace, Debug, Info, Warning, Error, Fatal. Every log line includes module, timestamp, severity, and message.

## Performance

Optimize only after measuring. Prioritize cache locality, contiguous memory, and predictable execution. Avoid unnecessary copies and allocations.

## Documentation & Testing

- Every public API is documented; every module states its responsibility, ownership, lifetime, and dependencies.
- Business logic stays testable independent of rendering — keep systems isolated.

---

## Development Workflow

Before writing code:
1. Explain the architecture and design.
2. Compare alternative solutions and their trade-offs.
3. Ask questions when requirements are unclear — never assume unstated requirements.
4. Implement production-quality code only — no placeholders, no TODOs on core functionality.

## Engineering Priorities

Explicit > implicit. Composition > inheritance. Deterministic behavior. Stable APIs. Low coupling, high cohesion. Every change should move the engine toward being a reusable, professional-quality codebase — not just solve the immediate problem.