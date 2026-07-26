/*
 * Framebuffer - an off-screen render target (color and/or depth
 * texture attachments, optionally multisampled), the prerequisite every
 * other Renderer expansion feature in this module builds on: shadow
 * maps render into a depth-only Framebuffer, HDR/bloom/post-processing
 * render into a floating-point color Framebuffer, MSAA is a
 * multisampled Framebuffer resolved into a regular one before anything
 * samples it as a texture (a multisampled texture cannot be sampled
 * with an ordinary sampler2D - resolving is mandatory, not optional,
 * before post-processing or presentation).
 *
 * Dependencies: Core, Graphics Context.
 */
#ifndef EISENFRONT_FRAMEBUFFER_H
#define EISENFRONT_FRAMEBUFFER_H

#include "core.h"
#include "graphics_context.h"

typedef enum FramebufferColorFormat {
    FRAMEBUFFER_COLOR_RGBA8 = 0, /* standard low dynamic range */
    FRAMEBUFFER_COLOR_RGBA16F,   /* HDR: values may exceed 1.0 before tone mapping */
} FramebufferColorFormat;

typedef struct FramebufferDesc {
    uint32_t                width;
    uint32_t                height;
    uint32_t                sample_count; /* 0 or 1 disables multisampling */
    bool                    has_color;
    FramebufferColorFormat  color_format;
    bool                    has_depth;
} FramebufferDesc;

typedef struct Framebuffer Framebuffer;

ENGINE_API Result framebuffer_create(const FramebufferDesc *desc, Framebuffer **out_framebuffer);
ENGINE_API void   framebuffer_destroy(Framebuffer *framebuffer);

/* Binds the framebuffer and sets the viewport to its full size. */
ENGINE_API void framebuffer_bind(const Framebuffer *framebuffer);
/* Binds the window system's default framebuffer (0) and sets the
 * viewport to the given size - the counterpart to framebuffer_bind()
 * for presenting the final image. */
ENGINE_API void framebuffer_bind_default(uint32_t width, uint32_t height);

/* 0 if the framebuffer has no color/depth attachment. A multisampled
 * attachment's texture name is a GL_TEXTURE_2D_MULTISAMPLE object -
 * resolve first (framebuffer_resolve_to) before sampling it normally. */
ENGINE_API uint32_t framebuffer_get_color_texture(const Framebuffer *framebuffer);
ENGINE_API uint32_t framebuffer_get_depth_texture(const Framebuffer *framebuffer);

ENGINE_API uint32_t framebuffer_get_width(const Framebuffer *framebuffer);
ENGINE_API uint32_t framebuffer_get_height(const Framebuffer *framebuffer);

/* Blits (resolves, if source is multisampled) src's color+depth into
 * dst; both must be the same size. This is how a multisampled render
 * target becomes a normal, sample-able texture. */
ENGINE_API Result framebuffer_resolve_to(const Framebuffer *src, const Framebuffer *dst);

#endif /* EISENFRONT_FRAMEBUFFER_H */
