/*
 * Texture - 2D textures, texture arrays, cubemaps, mipmaps, sampler
 * objects, and a path-keyed load cache, backed by stb_image for
 * decoding.
 *
 * Lifetime: exactly like ShaderProgram (see shader.h) - texture_load_2d
 * / texture_load_2d_array / texture_load_cubemap are cached and
 * reference-counted, keyed by their source path(s). Loading the same
 * path (or the same ordered set of paths, for arrays/cubemaps) twice
 * returns the same Texture with its ref count bumped, never a second
 * decode-and-upload. texture_release() drops one reference; the GL
 * texture is destroyed only once the count reaches zero.
 * texture_create_from_pixels() is for procedural/generated textures
 * with no backing file - it is reference-counted the same way but never
 * cached, since there is no path to key it on. texture_reload() hot-
 * reloads a path-loaded 2D texture in place; texture_decode_2d() /
 * texture_upload_2d() expose the CPU-decode/GL-upload split that both
 * texture_load_2d() and Asset Manager's async texture loading are built
 * from.
 *
 * Sampler objects: a Texture carries its own default sampling
 * parameters (set once, at creation, via TextureParams), but a Sampler
 * is a separate, independent object that - when bound to a texture
 * unit - overrides those defaults for whatever texture happens to be
 * bound to that same unit. This is what lets the same texture be
 * sampled two different ways at once (e.g. a shadow map read with
 * linear filtering during shading and with nearest filtering in a debug
 * view) without duplicating texture data.
 *
 * Dependencies: Core, Platform (file I/O), Graphics Context (GL), and
 * stb_image (privately, in texture.c only).
 */
#ifndef EISENFRONT_TEXTURE_H
#define EISENFRONT_TEXTURE_H

#include "core.h"
#include "graphics_context.h"

ENGINE_API Result texture_system_init(void);
ENGINE_API void   texture_system_shutdown(void);

typedef enum TextureFormat {
    TEXTURE_FORMAT_R8 = 0,
    TEXTURE_FORMAT_RG8,
    TEXTURE_FORMAT_RGB8,
    TEXTURE_FORMAT_RGBA8,
    TEXTURE_FORMAT_SRGB8_ALPHA8,
} TextureFormat;

typedef enum TextureWrap {
    TEXTURE_WRAP_REPEAT = 0,
    TEXTURE_WRAP_CLAMP_TO_EDGE,
    TEXTURE_WRAP_MIRRORED_REPEAT,
} TextureWrap;

typedef enum TextureFilter {
    TEXTURE_FILTER_NEAREST = 0,
    TEXTURE_FILTER_LINEAR,
    TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR,
} TextureFilter;

typedef struct TextureParams {
    TextureWrap   wrap_s, wrap_t, wrap_r;
    TextureFilter min_filter, mag_filter;
    float         max_anisotropy; /* 1.0 disables it; the driver clamps to its max */
    bool          generate_mipmaps;
} TextureParams;

ENGINE_API TextureParams texture_params_default(void);

typedef enum TextureType {
    TEXTURE_TYPE_2D = 0,
    TEXTURE_TYPE_2D_ARRAY,
    TEXTURE_TYPE_CUBEMAP,
} TextureType;

typedef struct Texture Texture;

ENGINE_API Result texture_load_2d(const char *path, const TextureParams *params,
                                   Texture **out_texture);

/* Re-decodes and re-uploads texture's original source path in place,
 * exactly like ShaderProgram's hot reload (see shader.h): existing
 * texture_get_gl_handle() values must be re-fetched afterward. Only
 * supported for TEXTURE_TYPE_2D; returns RESULT_ERROR_NOT_SUPPORTED for
 * arrays and cubemaps. Only valid for textures loaded from a path (not
 * texture_create_from_pixels()). */
ENGINE_API Result texture_reload(Texture *texture);

/* Split decode (pure CPU, thread-safe - safe to call from a worker
 * thread) / upload (GL calls, main-thread only) primitives that
 * texture_load_2d is itself built from. Asset Manager's async texture
 * loading uses exactly this split: decode on a worker thread, upload
 * during the next main-thread asset_manager_update(). */
typedef struct TextureCpuImage {
    void    *pixels;
    uint32_t width;
    uint32_t height;
} TextureCpuImage;

ENGINE_API Result texture_decode_2d(const char *path, TextureCpuImage *out_image);
ENGINE_API void   texture_free_cpu_image(TextureCpuImage *image);
ENGINE_API Result texture_upload_2d(const TextureCpuImage *image, const TextureParams *params,
                                     Texture **out_texture);

/* Every path must decode to the same width/height; each becomes one
 * array layer, in path order. */
ENGINE_API Result texture_load_2d_array(const char *const *paths, uint32_t layer_count,
                                         const TextureParams *params, Texture **out_texture);

/* paths[6] in [+X, -X, +Y, -Y, +Z, -Z] order; every face must decode to
 * the same square width == height. */
ENGINE_API Result texture_load_cubemap(const char *const paths[6], const TextureParams *params,
                                        Texture **out_texture);

/* Procedural/generated textures with no backing file - see the file
 * header comment on why these are never cached. pixels is tightly
 * packed (no row padding) in the layout TextureFormat implies. */
ENGINE_API Result texture_create_from_pixels(uint32_t width, uint32_t height, TextureFormat format,
                                              const void *pixels, const TextureParams *params,
                                              Texture **out_texture);

ENGINE_API void texture_retain(Texture *texture);
ENGINE_API void texture_release(Texture *texture);

ENGINE_API uint32_t    texture_get_gl_handle(const Texture *texture);
ENGINE_API TextureType texture_get_type(const Texture *texture);
ENGINE_API uint32_t    texture_get_width(const Texture *texture);
ENGINE_API uint32_t    texture_get_height(const Texture *texture);

/* glBindTextureUnit(unit, ...) - direct state access, no glActiveTexture
 * dance required. */
ENGINE_API void texture_bind(const Texture *texture, uint32_t unit);

typedef struct Sampler Sampler;

ENGINE_API Result sampler_create(const TextureParams *params, Sampler **out_sampler);
ENGINE_API void   sampler_destroy(Sampler *sampler);

/* Overrides the sampling parameters of whatever texture is bound to
 * this unit until sampler_unbind_unit() is called for that unit. */
ENGINE_API void sampler_bind(const Sampler *sampler, uint32_t unit);
ENGINE_API void sampler_unbind_unit(uint32_t unit);

#endif /* EISENFRONT_TEXTURE_H */
