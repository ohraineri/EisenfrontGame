/*
 * Editor - a Dear ImGui (via cimgui) debug UI overlay: init/shutdown,
 * per-frame pump, and rendering, wired to an existing Window and
 * GraphicsContext. Unlike Window/Input hiding SDL3, or Physics/Audio
 * wrapping their vendored library behind this engine's own API, Editor
 * follows Graphics Context's precedent instead: the wrapped library's C
 * API (cimgui.h, publicly included below) IS the point of this module,
 * so it is exposed directly rather than re-wrapped one igXxx() call at
 * a time - call igBegin()/igText()/igSliderFloat()/... straight from
 * game or engine debug code, exactly as Renderer/Shader/Texture call
 * gl* straight from Graphics Context's publicly exposed GLAD.
 *
 * The one deliberate exception to "this engine is ISO C23": Dear ImGui
 * itself is C++. cimgui.h is its `extern "C"` ABI boundary - everything
 * on the far side of it (imgui.cpp, cimgui.cpp, the SDL3/OpenGL3
 * backends) is vendored C++, built in third_party/cimgui and never
 * touched by this module's own source, which is ordinary C23 like
 * every other module.
 *
 * Build-time exclusion, not a runtime flag: this header - and the rest
 * of the Editor module - does not exist in a Release build at all (see
 * the EISENFRONT_BUILD_EDITOR CMake option, forced off whenever
 * CMAKE_BUILD_TYPE is Release). A debug overlay that ships because a
 * flag was left on is exactly the failure mode a compile-time
 * exclusion prevents; game code that calls into Editor must itself be
 * conditional on EISENFRONT_BUILD_EDITOR being defined.
 *
 * Scope, stated plainly rather than left implicit:
 *   - One ImGui context, one Window/GraphicsContext pair - no
 *     multi-viewport or docking (this vendors upstream Dear ImGui's
 *     master branch, not the docking branch) and no support for
 *     running two Editor overlays in one process.
 *   - editor_init() claims Window's single global raw-event slot (see
 *     window_set_raw_event_callback() in window.h) to feed SDL events
 *     to ImGui's input handling. Only one consumer of that slot can be
 *     registered at a time; a game that also needs raw SDL events
 *     while the Editor is active must be the one to chain the calls
 *     (there is no built-in multiplexing here, because Window itself
 *     does not support more than one registered raw-event callback) -
 *     editor_process_raw_event() is Editor's own handler exposed
 *     directly so a caller can do exactly that: call both _init()
 *     functions, then register one small combined WindowRawEventFn
 *     (after both, so it wins the slot) that calls
 *     input_process_raw_event() and editor_process_raw_event() in
 *     turn. Safe to call even when the Editor failed to initialize or
 *     was never initialized - it no-ops until editor_init() succeeds.
 *   - No ImGui .ini layout persistence (IniFilename is disabled) - a
 *     debug overlay's window layout is not treated as state worth
 *     saving between runs by default.
 *
 * Dependencies: Core, Window, Graphics Context, cimgui (publicly);
 * SDL3 (privately, in editor.c only, exactly like Window/Input/Graphics
 * Context already depend on it).
 */
#ifndef EISENFRONT_EDITOR_H
#define EISENFRONT_EDITOR_H

#include "core.h"
#include "graphics_context.h"
#include "window.h"

#include <cimgui.h>

ENGINE_API Result editor_init(Window *window, GraphicsContext *context);
ENGINE_API void   editor_shutdown(void);

/* Editor's own WindowRawEventFn, exposed so it can be chained into a
 * caller-owned combined callback - see the file header comment and
 * input.h's matching input_process_raw_event(). */
ENGINE_API void editor_process_raw_event(void *native_event, void *userdata);

/* Pumps the SDL3 and OpenGL3 backends' per-frame state and starts a new
 * ImGui frame (igNewFrame()). Call once per frame, after
 * window_poll_events(), before any ig* widget call. */
ENGINE_API void editor_new_frame(void);

/* Finalizes every ig* call made since editor_new_frame() and draws the
 * result with OpenGL. Call once per frame, after every widget call,
 * before graphics_context_swap_buffers(). */
ENGINE_API void editor_render(void);

/* True when ImGui wants exclusive use of the mouse/keyboard this frame
 * (e.g. a text field has focus, a window is being dragged) - check
 * before letting gameplay/camera input consume the same events. */
ENGINE_API bool editor_wants_capture_mouse(void);
ENGINE_API bool editor_wants_capture_keyboard(void);

#endif /* EISENFRONT_EDITOR_H */
