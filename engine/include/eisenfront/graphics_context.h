/*
 * Graphics Context - OpenGL 4.6 core context creation and function
 * loading, backed by SDL3 (for context creation/swap) and GLAD (for
 * function loading).
 *
 * Responsibility: turn a Window into a live, current OpenGL 4.6 core
 * context with every gl* function pointer loaded, optional debug output
 * wired to Core's logger, and VSync configured. This module does no
 * drawing - see Renderer for that.
 *
 * Unlike Window and Input, this header DOES expose OpenGL: it publicly
 * includes <glad/glad.h>. SDL is hidden here exactly like it is in
 * Window/Input (gameplay must never call SDL directly), but OpenGL is
 * not - Renderer, Shader, Buffers, Texture and Mesh are themselves the
 * GL abstraction layer and need real gl* calls. Graphics Context is the
 * seam that makes GL available to them; gameplay continues to never see
 * it, going through Renderer's command API instead.
 *
 * Dependencies: Core, Window, GLAD (publicly), SDL3 (privately, in
 * graphics_context.c only).
 *
 * Lifetime: exactly one GraphicsContext per Window for now (no
 * shared-context / multi-context support yet). Destroy the context
 * before destroying its Window.
 */
#ifndef EISENFRONT_GRAPHICS_CONTEXT_H
#define EISENFRONT_GRAPHICS_CONTEXT_H

#include "core.h"
#include "window.h"

#include <glad/glad.h>

typedef enum VSyncMode {
    VSYNC_OFF = 0,
    VSYNC_ON,
    VSYNC_ADAPTIVE, /* falls back to VSYNC_ON if the driver has no adaptive support */
} VSyncMode;

typedef struct GraphicsContextDesc {
    bool      debug;         /* request a debug context + wire glDebugMessageCallback */
    VSyncMode vsync;
    int32_t   msaa_samples;  /* 0 disables multisampling */
    int32_t   depth_bits;    /* 0 lets the desc default (24) apply */
    int32_t   stencil_bits;  /* 0 lets the desc default (8) apply */
} GraphicsContextDesc;

ENGINE_API GraphicsContextDesc graphics_context_desc_default(void);

typedef struct FramebufferInfo {
    int32_t red_bits, green_bits, blue_bits, alpha_bits;
    int32_t depth_bits;
    int32_t stencil_bits;
    int32_t msaa_samples;
    bool    double_buffered;
} FramebufferInfo;

typedef enum GraphicsDebugSeverity {
    GRAPHICS_DEBUG_SEVERITY_NOTIFICATION = 0,
    GRAPHICS_DEBUG_SEVERITY_LOW,
    GRAPHICS_DEBUG_SEVERITY_MEDIUM,
    GRAPHICS_DEBUG_SEVERITY_HIGH,
} GraphicsDebugSeverity;

typedef void (*GraphicsDebugMessageFn)(GraphicsDebugSeverity severity, const char *message,
                                        void *userdata);

typedef struct GraphicsContext GraphicsContext;

ENGINE_API Result graphics_context_create(Window *window, const GraphicsContextDesc *desc,
                                           GraphicsContext **out_context);
ENGINE_API void   graphics_context_destroy(GraphicsContext *context);

ENGINE_API Result graphics_context_make_current(GraphicsContext *context);
ENGINE_API void   graphics_context_swap_buffers(GraphicsContext *context);

ENGINE_API Result    graphics_context_set_vsync(GraphicsContext *context, VSyncMode mode);
ENGINE_API VSyncMode graphics_context_get_vsync(const GraphicsContext *context);

ENGINE_API void graphics_context_get_framebuffer_info(const GraphicsContext *context,
                                                        FramebufferInfo *out_info);

/* Called in addition to (never instead of) the automatic logging every
 * debug message already gets when GraphicsContextDesc.debug is true. */
ENGINE_API void graphics_context_set_debug_callback(GraphicsContext *context,
                                                      GraphicsDebugMessageFn fn, void *userdata);

ENGINE_API void graphics_context_get_version(const GraphicsContext *context, int32_t *out_major,
                                              int32_t *out_minor);

/* Opaque SDL_GLContext - the same deliberate exception as
 * window_get_native_handle() (see window.h's file header comment),
 * needed only by code that must hand this context to SDL/ImGui
 * directly (the Editor module's SDL3 backend init). Gameplay never
 * calls this. */
ENGINE_API void *graphics_context_get_native_handle(const GraphicsContext *context);

#endif /* EISENFRONT_GRAPHICS_CONTEXT_H */
