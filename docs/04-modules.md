# Module Guide

## Platform

Header: `engine/include/eisenfront/platform.h`

Responsibility: hide Linux, Windows, and macOS differences behind one engine API.

Features:

- Platform and compiler detection.
- Shared `Result` error enum.
- High-resolution monotonic time.
- Sleep and yield.
- Mutexes.
- Threads.
- Dynamic libraries.
- File I/O.
- Path operations.
- Directories and directory iterators.

Dependencies: none inside the engine.

## Core

Header: `engine/include/eisenfront/core.h`

Responsibility: engine lifecycle and common utilities.

Features:

- Configurable assertions.
- Thread-local error information.
- Logging with sinks.
- Stopwatch.
- FrameClock.
- ModuleRegistry.
- Engine lifecycle.

Minimal shape:

```c
Engine *engine = nullptr;
EngineConfig config = engine_config_default();
Result result = engine_create(&config, &engine);
```

## Window

Header: `engine/include/eisenfront/window.h`

Responsibility: create and manage windows without exposing SDL in the public API.

Features:

- Window system initialization and shutdown.
- Monitor information.
- Window creation and destruction.
- Windowed, borderless, fullscreen, and fullscreen borderless modes.
- Window-space size and pixel size.
- Display scale.
- Window event callbacks.
- Event polling.
- Internal raw event callback used by Input.

Dependencies: Core and private SDL3.

## Input

Header: `engine/include/eisenfront/input.h`

Responsibility: convert SDL events into keyboard, mouse, and gamepad state.

Frame contract:

```c
input_new_frame();
window_poll_events();
/* query pressed/held/released state */
```

Features:

- Common game keys.
- Pressed/released/held state.
- Mouse buttons.
- Mouse position, delta, and scroll.
- Relative mouse mode for FPS camera look.
- Connected gamepads.
- Gamepad buttons, axes, and vibration.

Dependencies: Core and Window.

## Graphics Context

Header: `engine/include/eisenfront/graphics_context.h`

Responsibility: create an OpenGL 4.6 core context, load GLAD, configure debug output, and manage VSync.

Features:

- `GraphicsContextDesc`.
- VSync off/on/adaptive.
- MSAA, depth, and stencil configuration.
- Debug callbacks.
- Framebuffer information.
- GL version query.
- Buffer swap.

Dependencies: Core, Window, public GLAD, private SDL3.

## Renderer

Header: `engine/include/eisenfront/renderer.h`

Responsibility: render frame lifecycle, command queue, sorting, and draw statistics.

Frame contract:

```c
renderer_begin_frame(renderer);
renderer_clear(renderer, &clear);
renderer_submit(renderer, &command);
renderer_end_frame(renderer);
graphics_context_swap_buffers(context);
```

Features:

- `RenderCommand`.
- Sorting by `sort_key`.
- Triangles, triangle strips, lines, line strips, and points.
- Indexed and non-indexed draws.
- Instancing.
- Viewport control.
- Wireframe mode.
- Draw call, triangle, state change, and frame statistics.

The renderer does not own GL resources. It executes commands referencing resources created elsewhere.

## Shader

Header: `engine/include/eisenfront/shader.h`

Responsibility: OpenGL shader program management.

Features:

- System initialization and shutdown.
- Vertex/fragment/geometry shader loading.
- Path-based cache.
- Retain/release ownership.
- Hot reload.
- GL handle access.
- Uniform setters.
- Uniform and attribute reflection.

Dependencies: Core, Graphics Context, and Platform.

## Buffers

Header: `engine/include/eisenfront/buffers.h`

Responsibility: wrappers for GL buffers and vertex arrays.

Features:

- Vertex, index, uniform, and storage buffers.
- Static, dynamic, and stream usage hints.
- Persistent mapped buffers.
- Region updates.
- Indexed binding for UBO/SSBO.
- Vertex attributes.
- Vertex arrays with optional index buffers.

Dependencies: Core and Graphics Context.

## Texture

Header: `engine/include/eisenfront/texture.h`

Responsibility: textures, arrays, cubemaps, samplers, and texture caching.

Features:

- Texture system lifecycle.
- 2D textures.
- Texture arrays.
- Cubemaps.
- Procedural textures from pixels.
- CPU decode separated from GL upload.
- Hot reload for path-loaded 2D textures.
- Reference counting.
- Sampler objects.

Dependencies: Core, Platform, Graphics Context, and private stb_image.

## Mesh

Header: `engine/include/eisenfront/mesh.h`

Responsibility: reusable geometry built on top of Buffers.

Features:

- Fixed interleaved vertex format.
- Position, normal, tangent, UV, bone indices, and bone weights.
- Static and dynamic meshes.
- Dynamic vertex/index updates.
- Instance buffer support.
- GL VAO handle access.

Dependencies: Core, Graphics Context, and Buffers.

## Material

Header: `engine/include/eisenfront/material.h`

Responsibility: group shader, textures, and PBR parameters.

Features:

- Material system lifecycle.
- Fixed texture slots: diffuse, normal, metallic, roughness, AO, emissive.
- Shared fallback textures.
- Material parameter UBO at `MATERIAL_UBO_BINDING`.
- `MaterialParams` with base color, emissive, metallic, roughness, and AO.
- Material instances with independent overrides.
- Complete shader, texture, and UBO binding.

Dependencies: Core, Shader, Texture, and Buffers.

## Asset Manager

Header: `engine/include/eisenfront/asset_manager.h`

Responsibility: path-keyed cache, handles, reference counts, and asset loading.

Asset types:

- Texture.
- Shader.
- Model.
- Audio data.
- Font data.

Features:

- Synchronous loads.
- Async loads for textures, models, audio data, and font data.
- Worker threads for CPU-side work.
- Main-thread GL upload through `asset_manager_update()`.
- Loaded callbacks.
- Loading, ready, and failed states.
- Typed access to ready resources.
- Synchronous reload.

Current limits:

- Shaders are synchronous only.
- glTF import currently focuses on geometry, not full material and animation pipelines.
- Audio and font assets are raw data blobs.

## Camera

Header: `engine/include/eisenfront/camera.h`

Responsibility: perspective/orthographic cameras, matrices, FPS-style movement, and frustum checks.

Features:

- Camera as a value type.
- Perspective and orthographic construction.
- View, projection, and view-projection matrices.
- Forward, right, and up vectors.
- Local movement.
- Yaw/pitch rotation with pitch clamp.
- Frustum extraction.
- Sphere and point tests against frustum.

Dependencies: Core and cglm.

## ECS

Header: `engine/include/eisenfront/ecs.h`

Responsibility: sparse-set Entity Component System.

Features:

- Entities as generational handles.
- Component type registration.
- One pool per component type.
- Components copied by value.
- Add/remove/has/get component operations.
- Queries with up to `ECS_MAX_QUERY_COMPONENTS`.
- Registered systems executed in order.

Tradeoff: each pool is allocated to `max_entities`, which favors simple behavior and predictable memory over tight memory usage for rarely used component types.

## Scene

Header: `engine/include/eisenfront/scene.h`

Responsibility: transform hierarchy and scene transitions.

Features:

- Generational `SceneNodeId`.
- Tree of nodes.
- Parent/child operations.
- Node names.
- Local position/rotation/scale.
- Cached world matrices.
- `scene_update_transforms()`.
- `SceneManager`.
- Load/unload callbacks during scene transition.

There is no serialized scene file format yet.

## Renderer FX

Headers:

- `framebuffer.h`
- `lighting.h`
- `skybox.h`
- `shadow.h`
- `postprocess.h`

Responsibility: rendering features beyond the base command renderer.

Features:

- Offscreen framebuffer with color/depth and MSAA.
- Framebuffer resolve.
- Directional, point, and spot light data in UBOs.
- Cubemap skybox.
- Single directional shadow map.
- HDR post-processing with MSAA, bloom, tone mapping, and gamma correction.

## Physics

Header: `engine/include/eisenfront/physics.h`

Responsibility in source: simple in-house rigid body simulation.

Exposed features:

- Sphere, box, and capsule shapes.
- Collision layers.
- Static, kinematic, and dynamic bodies.
- Gravity.
- Forces and impulses.
- Triggers.
- Raycast.
- Upright capsule character controller.

Documented scope:

- No angular dynamics.
- Box collision behaves as AABB collision.
- Pair testing is O(n^2).
- Some capsule rigid-body behavior uses approximation.
- Character controllers collide with the world, not with other controllers.

Build state: in the observed repository state, `physics.c` is not connected to the main engine CMake build.

