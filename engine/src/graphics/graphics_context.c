#include "eisenfront/graphics_context.h"

#include <SDL3/SDL.h>

#include <stdlib.h>

#define GRAPHICS_LOG_CATEGORY "graphics"

struct GraphicsContext {
    SDL_Window             *sdl_window;
    SDL_GLContext            gl_context;
    VSyncMode                vsync;
    FramebufferInfo           framebuffer;
    GraphicsDebugMessageFn   debug_fn;
    void                     *debug_userdata;
    int32_t                   version_major;
    int32_t                   version_minor;
};

GraphicsContextDesc graphics_context_desc_default(void) {
    return (GraphicsContextDesc){
        .debug = BUILD_DEBUG != 0,
        .vsync = VSYNC_ON,
        .msaa_samples = 0,
        .depth_bits = 24,
        .stencil_bits = 8,
    };
}

static void APIENTRY gl_debug_trampoline(GLenum source, GLenum type, GLuint id, GLenum severity,
                                          GLsizei length, const GLchar *message,
                                          const void *user_param) {
    (void)source;
    (void)type;
    (void)id;
    (void)length;
    GraphicsContext *context = (GraphicsContext *)user_param;

    GraphicsDebugSeverity neutral_severity;
    LogLevel              log_level;
    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:
            neutral_severity = GRAPHICS_DEBUG_SEVERITY_HIGH;
            log_level = LOG_LEVEL_ERROR;
            break;
        case GL_DEBUG_SEVERITY_MEDIUM:
            neutral_severity = GRAPHICS_DEBUG_SEVERITY_MEDIUM;
            log_level = LOG_LEVEL_WARN;
            break;
        case GL_DEBUG_SEVERITY_LOW:
            neutral_severity = GRAPHICS_DEBUG_SEVERITY_LOW;
            log_level = LOG_LEVEL_INFO;
            break;
        default:
            neutral_severity = GRAPHICS_DEBUG_SEVERITY_NOTIFICATION;
            log_level = LOG_LEVEL_DEBUG;
            break;
    }

    log_message(log_level, "gl", "%s", message);

    if (context != nullptr && context->debug_fn != nullptr) {
        context->debug_fn(neutral_severity, message, context->debug_userdata);
    }
}

Result graphics_context_create(Window *window, const GraphicsContextDesc *desc,
                                GraphicsContext **out_context) {
    if (window == nullptr || out_context == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const GraphicsContextDesc effective = (desc != nullptr) ? *desc : graphics_context_desc_default();
    SDL_Window *sdl_window = window_get_native_handle(window);

    /* Step 1 - request the pixel format and context type before creating
     * anything. SDL bakes these into the window's pixel format the first
     * time a GL context is created against it; none can change after. */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, effective.depth_bits > 0 ? effective.depth_bits : 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, effective.stencil_bits > 0 ? effective.stencil_bits : 8);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    if (effective.msaa_samples > 0) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, effective.msaa_samples);
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, effective.debug ? SDL_GL_CONTEXT_DEBUG_FLAG : 0);

    /* Step 2 - create the context against the window, using the
     * attributes just requested to pick a pixel format. */
    SDL_GLContext gl_context = SDL_GL_CreateContext(sdl_window);
    if (gl_context == nullptr) {
        LOG_ERROR(GRAPHICS_LOG_CATEGORY, "SDL_GL_CreateContext failed: %s", SDL_GetError());
        return RESULT_ERROR_PLATFORM;
    }

    /* Step 3 - bind the context to this thread. Every GL call, including
     * GLAD's own function-address queries in step 4, is only valid while
     * some context is current on the calling thread. */
    if (!SDL_GL_MakeCurrent(sdl_window, gl_context)) {
        LOG_ERROR(GRAPHICS_LOG_CATEGORY, "SDL_GL_MakeCurrent failed: %s", SDL_GetError());
        SDL_GL_DestroyContext(gl_context);
        return RESULT_ERROR_PLATFORM;
    }

    /* Step 4 - resolve every gl* function pointer through SDL's platform
     * loader (wglGetProcAddress / glXGetProcAddress / eglGetProcAddress).
     * This must happen after MakeCurrent: on WGL, the address returned
     * for a given function can depend on which context is current when
     * it is queried. */
    const int loaded_version = gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
    if (loaded_version == 0) {
        LOG_ERROR(GRAPHICS_LOG_CATEGORY, "GLAD failed to load OpenGL function pointers");
        SDL_GL_DestroyContext(gl_context);
        return RESULT_ERROR_PLATFORM;
    }

    /* Step 5 - SDL_GL_SetAttribute requests are hints, not guarantees;
     * confirm the driver actually granted at least 4.6 core before
     * anything above this module starts relying on 4.6-only entry points
     * (e.g. glDebugMessageCallback needs no extension check because it
     * is core as of 4.5, but future modules may rely on other 4.6-only
     * features such as glSpecializeShader). */
    if (GLVersion.major * 10 + GLVersion.minor < 46) {
        LOG_ERROR(GRAPHICS_LOG_CATEGORY, "OpenGL %d.%d is not sufficient, need at least 4.6",
                  GLVersion.major, GLVersion.minor);
        SDL_GL_DestroyContext(gl_context);
        return RESULT_ERROR_NOT_SUPPORTED;
    }

    GraphicsContext *context = malloc(sizeof(*context));
    if (context == nullptr) {
        SDL_GL_DestroyContext(gl_context);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    context->sdl_window = sdl_window;
    context->gl_context = gl_context;
    context->vsync = effective.vsync;
    context->debug_fn = nullptr;
    context->debug_userdata = nullptr;
    context->version_major = GLVersion.major;
    context->version_minor = GLVersion.minor;

    /* Step 6 - debug output. GL_DEBUG_OUTPUT_SYNCHRONOUS makes the
     * callback fire on the same call stack as the GL call that triggered
     * it; without it, drivers are free to report asynchronously from
     * another thread at an arbitrary later time, which makes the message
     * useless for breaking in a debugger at the point of the error.
     * glDebugMessageControl(..., GL_TRUE) with GL_DONT_CARE everywhere
     * means "report every source/type/severity"; callers narrow this
     * later if notification spam becomes a problem. */
    if (effective.debug) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(gl_debug_trampoline, context);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
    }

    /* Step 7 - VSync. This must happen after MakeCurrent: the swap
     * interval is per-context state (WGL_EXT_swap_control /
     * GLX_EXT_swap_control / EGL), not a global setting. */
    graphics_context_set_vsync(context, effective.vsync);

    /* Step 8 - read back what the driver actually granted for the
     * framebuffer. Requested bit depths and sample counts from step 1
     * are hints the driver is free to round to the nearest format it
     * actually supports, and are queried straight from GL rather than
     * via SDL_GL_GetAttribute: the latter only reflects the EGL/GLX
     * config attribute list, which not every backend populates (this
     * engine's own offscreen/EGL test backend does not); querying the
     * default framebuffer's attachments directly is the core-profile-
     * correct way to ask GL itself, and always works. */
    GLint color_bits = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_BACK_LEFT,
                                           GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE, &color_bits);
    context->framebuffer.red_bits = color_bits;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_BACK_LEFT,
                                           GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE, &color_bits);
    context->framebuffer.green_bits = color_bits;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_BACK_LEFT,
                                           GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE, &color_bits);
    context->framebuffer.blue_bits = color_bits;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_BACK_LEFT,
                                           GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE, &color_bits);
    context->framebuffer.alpha_bits = color_bits;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH,
                                           GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &color_bits);
    context->framebuffer.depth_bits = color_bits;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_STENCIL,
                                           GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &color_bits);
    context->framebuffer.stencil_bits = color_bits;

    GLint samples = 0;
    glGetIntegerv(GL_SAMPLES, &samples);
    context->framebuffer.msaa_samples = samples;

    GLboolean double_buffered = GL_FALSE;
    glGetBooleanv(GL_DOUBLEBUFFER, &double_buffered);
    context->framebuffer.double_buffered = (double_buffered != GL_FALSE);

    LOG_INFO(GRAPHICS_LOG_CATEGORY, "OpenGL %d.%d core context created (%s / %s)",
             context->version_major, context->version_minor, (const char *)glGetString(GL_RENDERER),
             (const char *)glGetString(GL_VENDOR));

    *out_context = context;
    return RESULT_OK;
}

void graphics_context_destroy(GraphicsContext *context) {
    if (context == nullptr) {
        return;
    }
    SDL_GL_DestroyContext(context->gl_context);
    free(context);
}

Result graphics_context_make_current(GraphicsContext *context) {
    if (context == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!SDL_GL_MakeCurrent(context->sdl_window, context->gl_context)) {
        return RESULT_ERROR_PLATFORM;
    }
    return RESULT_OK;
}

void graphics_context_swap_buffers(GraphicsContext *context) {
    ASSERT(context != nullptr);
    if (context == nullptr) {
        return;
    }
    SDL_GL_SwapWindow(context->sdl_window);
}

Result graphics_context_set_vsync(GraphicsContext *context, VSyncMode mode) {
    if (context == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    int interval = 0;
    switch (mode) {
        case VSYNC_OFF:
            interval = 0;
            break;
        case VSYNC_ON:
            interval = 1;
            break;
        case VSYNC_ADAPTIVE:
            interval = -1;
            break;
        default:
            return RESULT_ERROR_INVALID_ARGUMENT;
    }

    if (!SDL_GL_SetSwapInterval(interval)) {
        if (mode != VSYNC_ADAPTIVE) {
            return RESULT_ERROR_PLATFORM;
        }
        /* Not every driver supports adaptive vsync; fall back to regular
         * vsync instead of failing outright. */
        if (!SDL_GL_SetSwapInterval(1)) {
            return RESULT_ERROR_PLATFORM;
        }
        mode = VSYNC_ON;
    }

    context->vsync = mode;
    return RESULT_OK;
}

VSyncMode graphics_context_get_vsync(const GraphicsContext *context) {
    ASSERT(context != nullptr);
    return context != nullptr ? context->vsync : VSYNC_ON;
}

void graphics_context_get_framebuffer_info(const GraphicsContext *context,
                                            FramebufferInfo *out_info) {
    ASSERT(context != nullptr);
    ASSERT(out_info != nullptr);
    if (context == nullptr || out_info == nullptr) {
        return;
    }
    *out_info = context->framebuffer;
}

void graphics_context_set_debug_callback(GraphicsContext *context, GraphicsDebugMessageFn fn,
                                          void *userdata) {
    ASSERT(context != nullptr);
    if (context == nullptr) {
        return;
    }
    context->debug_fn = fn;
    context->debug_userdata = userdata;
}

void graphics_context_get_version(const GraphicsContext *context, int32_t *out_major,
                                   int32_t *out_minor) {
    ASSERT(context != nullptr);
    if (context == nullptr) {
        return;
    }
    if (out_major != nullptr) {
        *out_major = context->version_major;
    }
    if (out_minor != nullptr) {
        *out_minor = context->version_minor;
    }
}

void *graphics_context_get_native_handle(const GraphicsContext *context) {
    ASSERT(context != nullptr);
    return context != nullptr ? context->gl_context : nullptr;
}
