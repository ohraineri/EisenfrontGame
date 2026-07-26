/*
 * PostProcess - an HDR render target plus a fullscreen post-processing
 * pipeline: optional MSAA (resolved before anything samples the
 * image), optional bloom, tone mapping, and gamma correction, each a
 * separate, independently-understandable pass over the same design
 * (Framebuffer in, a small embedded fullscreen-quad shader, Framebuffer
 * out) - "modular" in the sense the task asked for: bloom can be
 * disabled entirely (PostProcessDesc.enable_bloom) without touching
 * tone mapping or gamma correction, which always run.
 *
 * Pipeline, in order:
 *   1. The game renders normally into postprocess_begin_scene()'s
 *      framebuffer - an RGBA16F (HDR: values may exceed 1.0) target,
 *      optionally multisampled for MSAA.
 *   2. postprocess_resolve_and_apply() resolves MSAA if enabled
 *      (framebuffer_resolve_to - a multisampled texture cannot be
 *      sampled directly).
 *   3. If bloom is enabled: a bright-pass extracts pixels above a
 *      threshold into a half-resolution buffer, then a separable
 *      (horizontal, then vertical) Gaussian blur is applied to it
 *      across several ping-pong passes between two buffers - the
 *      standard technique for a cheap, reasonably smooth blur without
 *      one enormous single-pass kernel.
 *   4. A final composite pass adds the (possibly zero) bloom result to
 *      the resolved HDR color, applies Reinhard tone mapping
 *      (color / (color + 1)) to compress the unbounded HDR range into
 *      [0,1] - this is what "tone mapping" means: deciding how an HDR
 *      image's highlights map down to a displayable range, the same
 *      role photographic exposure/dynamic-range compression plays for
 *      a camera - then gamma-correct (pow(color, 1/2.2)) since the
 *      default framebuffer expects gamma-encoded (sRGB-ish) output
 *      while every calculation up to this point was done in linear
 *      light, and writes the result to the caller-selected output
 *      framebuffer (typically the window's default framebuffer).
 *
 * Dependencies: Core, Graphics Context, Buffers, Framebuffer.
 */
#ifndef EISENFRONT_POSTPROCESS_H
#define EISENFRONT_POSTPROCESS_H

#include "buffers.h"
#include "core.h"
#include "framebuffer.h"
#include "graphics_context.h"

typedef struct PostProcessDesc {
    uint32_t width;
    uint32_t height;
    uint32_t msaa_samples;  /* 0 or 1 disables MSAA */
    bool     enable_bloom;
    float    exposure;      /* Reinhard tone mapping input scale; 1.0 is neutral */
    float    bloom_threshold; /* luminance above which a pixel contributes to bloom */
} PostProcessDesc;

ENGINE_API PostProcessDesc postprocess_desc_default(uint32_t width, uint32_t height);

typedef struct PostProcess PostProcess;

ENGINE_API Result postprocess_create(const PostProcessDesc *desc, PostProcess **out_postprocess);
ENGINE_API void   postprocess_destroy(PostProcess *postprocess);

/* Binds the HDR scene framebuffer (with depth) - render the frame's
 * normal 3D content after calling this instead of binding a
 * Framebuffer directly. */
ENGINE_API void postprocess_begin_scene(PostProcess *postprocess);

/* Runs the full pipeline described in the file header comment and
 * writes the final image to whatever framebuffer is bound to
 * GL_DRAW_FRAMEBUFFER when this is called - call
 * framebuffer_bind_default() (or framebuffer_bind() for a different
 * target) immediately before this. output_width/output_height set the
 * viewport for that final pass. */
ENGINE_API void postprocess_resolve_and_apply(PostProcess *postprocess, uint32_t output_width,
                                               uint32_t output_height);

ENGINE_API void postprocess_set_exposure(PostProcess *postprocess, float exposure);

#endif /* EISENFRONT_POSTPROCESS_H */
