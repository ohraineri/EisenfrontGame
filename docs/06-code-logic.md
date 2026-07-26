# Code Logic And Design Reasoning

This document explains the reasoning behind some important parts of the codebase. It is meant to help new contributors understand why the code is shaped the way it is, not just what each function does.

## Why Platform Exists

The Platform module is the engine's boundary with the operating system. File APIs, threads, mutexes, dynamic libraries, paths, and time are all common needs, but each OS exposes them differently. Putting that logic in one layer keeps the rest of the engine portable.

The alternative would be scattering `#ifdef _WIN32`, POSIX headers, and platform-specific behavior through higher-level modules. That would make every module harder to reason about and harder to test.

## Why Most APIs Return `Result`

C does not have exceptions. Eisenfront uses `Result` as a simple, explicit failure channel. A function that can fail returns a code, and richer diagnostics can be attached through Core's error system.

This keeps ownership and control flow visible:

```c
Result result = texture_load_2d(path, &params, &texture);
if (result != RESULT_OK) {
    return result;
}
```

This style is deliberately boring. In C, boring error handling is often the reliable kind.

## Why SDL Is Hidden

SDL is used for windows, input, and GL context creation, but SDL types are not meant to leak into gameplay-facing APIs. `Window` owns SDL window details, `Input` consumes raw event callbacks, and `Graphics Context` uses SDL only where a GL context must be created.

That allows the engine to replace, isolate, or test SDL-backed behavior without making gameplay depend on SDL directly.

## Why OpenGL Is Public To Internal Graphics Modules

Unlike SDL, OpenGL is intentionally exposed through `graphics_context.h` because Shader, Buffers, Texture, Mesh, Renderer, and Renderer FX are the graphics abstraction layer. These modules need real GL types and functions.

The boundary is not "no code may see GL." The boundary is "gameplay should not need to call GL."

## Why Renderer Uses Raw GL Handles

`RenderCommand` stores raw GL object names for vertex arrays and shader programs. That keeps Renderer independent from Mesh, Shader, Material, and future resource wrappers.

The benefit is low coupling: Renderer can exist early and execute draw commands without knowing which module created the VAO or shader.

The cost is less type safety. A future higher-level scene renderer can sit above this command API and provide safer integration without removing the lower-level renderer.

## Why The Renderer Has A Begin/End Frame Contract

The renderer owns per-frame command storage and statistics. `renderer_begin_frame()` resets the queue and stats. `renderer_submit()` appends commands. `renderer_end_frame()` sorts and executes them.

This makes frame ownership explicit:

```text
begin frame -> collect commands -> sort/execute -> present elsewhere
```

Presentation belongs to Graphics Context because not every render target is necessarily a window backbuffer.

## Why Input Requires A Specific Frame Order

Input tracks "current" and "previous" state. Pressed and released are not stored as permanent facts; they are computed by comparing this frame against the previous one.

That is why the order matters:

```text
input_new_frame()
window_poll_events()
query input
```

If `window_poll_events()` ran before `input_new_frame()`, the new events could be snapshot as old state and edge detection would become wrong.

## Why Texture Decode And Upload Are Split

Image decoding is CPU work and can happen on a worker thread. Texture upload is OpenGL work and must happen on the thread with the active GL context.

The split enables async loading:

```text
worker thread: read file + decode pixels
main thread: upload pixels to GL
```

The Asset Manager uses this design for async texture loading.

## Why Shader Loading Is Not Async

Shader compilation calls OpenGL. Because that requires a valid GL context, it is not split into a CPU-only worker part in the same way texture decoding is.

A future renderer could support background shader preprocessing, file reads, or reflection cache reads, but actual GL compilation still needs careful context ownership.

## Why Textures And Shaders Are Cached By Path

Loading the same texture or shader twice should not duplicate GPU memory or compile duplicate programs. The cache maps source identity to the already loaded resource and increments a reference count.

This supports natural usage:

```text
system A loads "brick.png"
system B loads "brick.png"
both receive the same underlying Texture with refcount 2
```

When both release it, the resource is destroyed.

## Why Material Has Fixed Texture Units

Material binds diffuse, normal, metallic, roughness, AO, and emissive textures to fixed texture units. This creates a stable shader convention: any material-compatible shader can assume the same layout.

The benefit is simple binding code and fewer per-material decisions. The cost is less flexibility than a fully dynamic descriptor/bindless system. For this engine stage, the fixed convention is easier to test and reason about.

## Why Material Uses Fallback Textures

A material may omit a texture slot. Instead of forcing every shader to branch around missing textures, the material system binds a default texture:

- White for diffuse, metallic, roughness, AO, and emissive.
- Flat normal for normal maps.

This lets shaders sample every slot unconditionally.

## Why Mesh Uses One Fixed Vertex Layout

The Mesh module uses one interleaved `Vertex` format with positions, normals, tangents, UVs, and skinning fields. That is less flexible than arbitrary vertex layouts, but it keeps the engine's mesh path small and consistent.

The tradeoff is intentional:

- One VAO layout.
- One shader convention.
- Simpler asset import.
- Easier tests.

Specialized streaming or unusual layouts can still use Buffers directly.

## Why ECS Uses Sparse Sets

Each component type owns a dense component array and a sparse lookup table. This gives:

- O(1) add/remove/has/get operations.
- Dense iteration for cache-friendly systems.
- Entity IDs that can be checked against generation counters.

Removal swaps the last dense element into the removed slot. That makes removal O(1), but it invalidates pointers into that component pool. The header calls this out because holding component pointers across removals would be unsafe.

## Why ECS Allocates Pools Up Front

Each registered component pool is allocated at `max_entities` capacity. This avoids reallocations and keeps behavior predictable.

The cost is memory usage: rare components still reserve space for every possible entity. A more complex engine might chunk-allocate pools, but fixed capacity is simpler and reliable at this stage.

## Why Scene Uses Generational Handles

Scene nodes are identified by `SceneNodeId`, not raw pointers. The ID stores both a slot index and a generation. When a node is destroyed, the generation changes, so stale IDs can be detected.

This avoids a common bug: destroying an object and later accidentally using an old handle that now points to a different object in the reused slot.

## Why Scene World Matrices Are Updated Explicitly

`scene_update_transforms()` recomputes world matrices for all nodes. Reads after that return the cached result.

This avoids hidden recursive recomputation on every matrix read and makes the frame contract clear:

```text
mutate local transforms -> update scene transforms -> read world matrices
```

## Why Asset Manager Separates Handle Status From Resource Access

Async loading means a handle may exist before the resource is ready. The handle status tells callers whether the asset is loading, ready, or failed.

Typed accessors only return usable resources when the handle is ready. This prevents code from treating a pending asset as a complete one.

## Why Renderer FX Is Separate From Renderer

The base Renderer executes render commands. Renderer FX adds higher-level rendering features: framebuffers, lighting UBOs, skybox, shadow maps, and post-processing.

Keeping these separate avoids turning the base renderer into a large feature bundle. It also lets lower-level renderer tests stay focused.

## Why Physics Has A Clearly Limited Scope

The Physics API documents its limits directly: no angular dynamics, AABB-style box collision, O(n^2) pair checks, and a simple upright character controller.

This is useful engineering honesty. A limited physics system can still be valuable if its behavior is explicit. Hidden "almost full physics" behavior would be much harder to trust.

## How To Read An Implementation File

When reading a `.c` file, look for these patterns:

- Module-level static state.
- Validation at the top of public functions.
- Ownership transfer points.
- Result codes returned on each failure path.
- Cleanup paths on partial failure.
- Calls into lower layers only.
- Matching create/destroy or retain/release symmetry.

This project rewards reading from API contract to implementation to tests.

