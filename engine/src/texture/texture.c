#include "eisenfront/texture.h"

#include "eisenfront/platform.h"

#include <stb_image.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXTURE_LOG_CATEGORY "texture"
#define TEXTURE_KEY_CAPACITY 1536u
#define TEXTURE_CACHE_CAPACITY 256u
#define TEXTURE_MAX_ARRAY_LAYERS 256u

struct Texture {
    uint32_t    gl_texture;
    TextureType type;
    uint32_t    width;
    uint32_t    height;
    uint32_t    ref_count;
    /* Populated only for a texture loaded by texture_load_2d(); empty
     * for arrays, cubemaps, and texture_create_from_pixels() results.
     * texture_reload() needs it to know what to re-decode. */
    char          source_path[512];
    TextureParams params; /* preserved across texture_reload() */
};

struct Sampler {
    uint32_t gl_sampler;
};

typedef struct TextureCacheEntry {
    char     key[TEXTURE_KEY_CAPACITY];
    Texture *texture;
} TextureCacheEntry;

static bool              g_initialized = false;
static TextureCacheEntry g_cache[TEXTURE_CACHE_CAPACITY];
static uint32_t          g_cache_count = 0;

Result texture_system_init(void) {
    if (g_initialized) {
        return RESULT_ERROR_ALREADY_INITIALIZED;
    }
    memset(g_cache, 0, sizeof(g_cache));
    g_cache_count = 0;
    g_initialized = true;
    return RESULT_OK;
}

void texture_system_shutdown(void) {
    if (!g_initialized) {
        return;
    }
    if (g_cache_count > 0) {
        LOG_WARN(TEXTURE_LOG_CATEGORY,
                 "texture_system_shutdown with %u texture(s) still cached; releasing them",
                 g_cache_count);
    }
    for (uint32_t i = 0; i < g_cache_count; ++i) {
        glDeleteTextures(1, &g_cache[i].texture->gl_texture);
        free(g_cache[i].texture);
    }
    g_cache_count = 0;
    g_initialized = false;
}

TextureParams texture_params_default(void) {
    return (TextureParams){
        .wrap_s = TEXTURE_WRAP_REPEAT,
        .wrap_t = TEXTURE_WRAP_REPEAT,
        .wrap_r = TEXTURE_WRAP_REPEAT,
        .min_filter = TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR,
        .mag_filter = TEXTURE_FILTER_LINEAR,
        .max_anisotropy = 1.0f,
        .generate_mipmaps = true,
    };
}

static Texture *cache_find(const char *key) {
    for (uint32_t i = 0; i < g_cache_count; ++i) {
        if (strcmp(g_cache[i].key, key) == 0) {
            return g_cache[i].texture;
        }
    }
    return nullptr;
}

static void cache_insert(const char *key, Texture *texture) {
    if (g_cache_count < TEXTURE_CACHE_CAPACITY) {
        snprintf(g_cache[g_cache_count].key, sizeof(g_cache[g_cache_count].key), "%s", key);
        g_cache[g_cache_count].texture = texture;
        g_cache_count += 1;
    } else {
        LOG_WARN(TEXTURE_LOG_CATEGORY, "Texture cache full (%u); '%s' will reload on next request",
                 TEXTURE_CACHE_CAPACITY, key);
    }
}

static void build_joined_key(const char *const *paths, uint32_t count, char *out_key,
                              size_t capacity) {
    size_t offset = 0;
    for (uint32_t i = 0; i < count && offset < capacity; ++i) {
        const int written = snprintf(out_key + offset, capacity - offset, "%s%s", paths[i],
                                      (i + 1 < count) ? "|" : "");
        if (written < 0) {
            break;
        }
        offset += (size_t)written;
    }
}

/* Internal helper shared by texture_decode_2d() and the array/cubemap
 * loaders below, which decode straight into their own stack-local
 * per-face/per-layer structs rather than the public TextureCpuImage. */
typedef struct DecodedImage {
    unsigned char *pixels;
    int            width;
    int            height;
} DecodedImage;

static Result decode_image_from_path(const char *path, DecodedImage *out_image) {
    void  *file_data = nullptr;
    size_t file_size = 0;
    Result result = file_read_all(path, &file_data, &file_size);
    if (result != RESULT_OK) {
        LOG_ERROR(TEXTURE_LOG_CATEGORY, "Failed to read texture file '%s': %s", path,
                  result_to_string(result));
        return result;
    }

    int channels_in_file = 0;
    /* Always decode to 4 channels: an arbitrary-width RGB8 image's rows
     * are not guaranteed 4-byte aligned, a classic OpenGL upload pitfall
     * that forcing RGBA8 sidesteps entirely, and it keeps every loaded
     * texture uniform for the shading pipeline. */
    unsigned char *pixels =
        stbi_load_from_memory((const unsigned char *)file_data, (int)file_size, &out_image->width,
                               &out_image->height, &channels_in_file, STBI_rgb_alpha);
    file_free_buffer(file_data);

    if (pixels == nullptr) {
        LOG_ERROR(TEXTURE_LOG_CATEGORY, "Failed to decode image '%s': %s", path,
                  stbi_failure_reason());
        return RESULT_ERROR_PLATFORM;
    }

    out_image->pixels = pixels;
    return RESULT_OK;
}

static void free_decoded_image(DecodedImage *image) {
    stbi_image_free(image->pixels);
    image->pixels = nullptr;
}

Result texture_decode_2d(const char *path, TextureCpuImage *out_image) {
    if (path == nullptr || out_image == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    DecodedImage decoded;
    const Result result = decode_image_from_path(path, &decoded);
    if (result != RESULT_OK) {
        return result;
    }
    out_image->pixels = decoded.pixels;
    out_image->width = (uint32_t)decoded.width;
    out_image->height = (uint32_t)decoded.height;
    return RESULT_OK;
}

void texture_free_cpu_image(TextureCpuImage *image) {
    if (image == nullptr) {
        return;
    }
    stbi_image_free(image->pixels);
    image->pixels = nullptr;
}

typedef struct GlFormatMapping {
    GLenum internal_format;
    GLenum data_format;
    GLenum data_type;
} GlFormatMapping;

static GlFormatMapping gl_format_for(TextureFormat format) {
    switch (format) {
        case TEXTURE_FORMAT_R8:
            return (GlFormatMapping){GL_R8, GL_RED, GL_UNSIGNED_BYTE};
        case TEXTURE_FORMAT_RG8:
            return (GlFormatMapping){GL_RG8, GL_RG, GL_UNSIGNED_BYTE};
        case TEXTURE_FORMAT_RGB8:
            return (GlFormatMapping){GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE};
        case TEXTURE_FORMAT_SRGB8_ALPHA8:
            return (GlFormatMapping){GL_SRGB8_ALPHA8, GL_RGBA, GL_UNSIGNED_BYTE};
        case TEXTURE_FORMAT_RGBA8:
        default:
            return (GlFormatMapping){GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
    }
}

static GLint gl_wrap_for(TextureWrap wrap) {
    switch (wrap) {
        case TEXTURE_WRAP_CLAMP_TO_EDGE:
            return GL_CLAMP_TO_EDGE;
        case TEXTURE_WRAP_MIRRORED_REPEAT:
            return GL_MIRRORED_REPEAT;
        case TEXTURE_WRAP_REPEAT:
        default:
            return GL_REPEAT;
    }
}

static GLint gl_filter_for(TextureFilter filter) {
    switch (filter) {
        case TEXTURE_FILTER_NEAREST:
            return GL_NEAREST;
        case TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
            return GL_LINEAR_MIPMAP_LINEAR;
        case TEXTURE_FILTER_LINEAR:
        default:
            return GL_LINEAR;
    }
}

static GLsizei mip_level_count(uint32_t width, uint32_t height) {
    GLsizei  levels = 1;
    uint32_t size = width > height ? width : height;
    while (size > 1) {
        size >>= 1;
        levels += 1;
    }
    return levels;
}

static void apply_texture_params(GLuint gl_texture, const TextureParams *params) {
    glTextureParameteri(gl_texture, GL_TEXTURE_WRAP_S, gl_wrap_for(params->wrap_s));
    glTextureParameteri(gl_texture, GL_TEXTURE_WRAP_T, gl_wrap_for(params->wrap_t));
    glTextureParameteri(gl_texture, GL_TEXTURE_WRAP_R, gl_wrap_for(params->wrap_r));
    glTextureParameteri(gl_texture, GL_TEXTURE_MIN_FILTER, gl_filter_for(params->min_filter));
    glTextureParameteri(gl_texture, GL_TEXTURE_MAG_FILTER, gl_filter_for(params->mag_filter));
    if (params->max_anisotropy > 1.0f) {
        glTextureParameterf(gl_texture, GL_TEXTURE_MAX_ANISOTROPY, params->max_anisotropy);
    }
}

static void apply_sampler_params(GLuint gl_sampler, const TextureParams *params) {
    glSamplerParameteri(gl_sampler, GL_TEXTURE_WRAP_S, gl_wrap_for(params->wrap_s));
    glSamplerParameteri(gl_sampler, GL_TEXTURE_WRAP_T, gl_wrap_for(params->wrap_t));
    glSamplerParameteri(gl_sampler, GL_TEXTURE_WRAP_R, gl_wrap_for(params->wrap_r));
    glSamplerParameteri(gl_sampler, GL_TEXTURE_MIN_FILTER, gl_filter_for(params->min_filter));
    glSamplerParameteri(gl_sampler, GL_TEXTURE_MAG_FILTER, gl_filter_for(params->mag_filter));
    if (params->max_anisotropy > 1.0f) {
        glSamplerParameterf(gl_sampler, GL_TEXTURE_MAX_ANISOTROPY, params->max_anisotropy);
    }
}

Result texture_upload_2d(const TextureCpuImage *image, const TextureParams *params,
                          Texture **out_texture) {
    if (image == nullptr || image->pixels == nullptr || out_texture == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const TextureParams effective = (params != nullptr) ? *params : texture_params_default();

    GLuint gl_texture = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &gl_texture);
    const GLsizei levels =
        effective.generate_mipmaps ? mip_level_count(image->width, image->height) : 1;
    glTextureStorage2D(gl_texture, levels, GL_RGBA8, (GLsizei)image->width, (GLsizei)image->height);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(gl_texture, 0, 0, 0, (GLsizei)image->width, (GLsizei)image->height,
                         GL_RGBA, GL_UNSIGNED_BYTE, image->pixels);
    if (effective.generate_mipmaps) {
        glGenerateTextureMipmap(gl_texture);
    }
    apply_texture_params(gl_texture, &effective);

    Texture *texture = malloc(sizeof(*texture));
    if (texture == nullptr) {
        glDeleteTextures(1, &gl_texture);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    texture->gl_texture = gl_texture;
    texture->type = TEXTURE_TYPE_2D;
    texture->width = image->width;
    texture->height = image->height;
    texture->ref_count = 1;
    texture->source_path[0] = '\0';
    texture->params = effective;

    *out_texture = texture;
    return RESULT_OK;
}

Result texture_load_2d(const char *path, const TextureParams *params, Texture **out_texture) {
    if (path == nullptr || out_texture == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return RESULT_ERROR_NOT_INITIALIZED;
    }

    Texture *cached = cache_find(path);
    if (cached != nullptr) {
        cached->ref_count += 1;
        *out_texture = cached;
        return RESULT_OK;
    }

    TextureCpuImage image;
    Result          result = texture_decode_2d(path, &image);
    if (result != RESULT_OK) {
        return result;
    }

    Texture *texture = nullptr;
    result = texture_upload_2d(&image, params, &texture);
    texture_free_cpu_image(&image);
    if (result != RESULT_OK) {
        return result;
    }

    snprintf(texture->source_path, sizeof(texture->source_path), "%s", path);
    cache_insert(path, texture);
    LOG_INFO(TEXTURE_LOG_CATEGORY, "Loaded texture '%s' (%ux%u)", path, texture->width,
             texture->height);

    *out_texture = texture;
    return RESULT_OK;
}

Result texture_reload(Texture *texture) {
    ASSERT(texture != nullptr);
    if (texture == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (texture->type != TEXTURE_TYPE_2D) {
        return RESULT_ERROR_NOT_SUPPORTED;
    }
    if (texture->source_path[0] == '\0') {
        return RESULT_ERROR_INVALID_STATE;
    }

    TextureCpuImage image;
    Result          result = texture_decode_2d(texture->source_path, &image);
    if (result != RESULT_OK) {
        LOG_WARN(TEXTURE_LOG_CATEGORY, "Hot reload failed for '%s'; keeping the last working texture",
                 texture->source_path);
        return result;
    }

    Texture *replacement = nullptr;
    result = texture_upload_2d(&image, &texture->params, &replacement);
    texture_free_cpu_image(&image);
    if (result != RESULT_OK) {
        LOG_WARN(TEXTURE_LOG_CATEGORY, "Hot reload failed for '%s'; keeping the last working texture",
                 texture->source_path);
        return result;
    }

    /* Swap the GL object in place - texture (the pointer every caller
     * holds) keeps its identity, exactly like ShaderProgram's reload. */
    const GLuint old_gl_texture = texture->gl_texture;
    texture->gl_texture = replacement->gl_texture;
    texture->width = replacement->width;
    texture->height = replacement->height;
    free(replacement);

    glDeleteTextures(1, &old_gl_texture);
    LOG_INFO(TEXTURE_LOG_CATEGORY, "Reloaded texture '%s'", texture->source_path);
    return RESULT_OK;
}

Result texture_load_2d_array(const char *const *paths, uint32_t layer_count,
                              const TextureParams *params, Texture **out_texture) {
    if (paths == nullptr || layer_count == 0 || out_texture == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return RESULT_ERROR_NOT_INITIALIZED;
    }
    if (layer_count > TEXTURE_MAX_ARRAY_LAYERS) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }

    char key[TEXTURE_KEY_CAPACITY];
    build_joined_key(paths, layer_count, key, sizeof(key));

    Texture *cached = cache_find(key);
    if (cached != nullptr) {
        cached->ref_count += 1;
        *out_texture = cached;
        return RESULT_OK;
    }

    DecodedImage images[TEXTURE_MAX_ARRAY_LAYERS];
    for (uint32_t i = 0; i < layer_count; ++i) {
        const Result result = decode_image_from_path(paths[i], &images[i]);
        if (result != RESULT_OK) {
            for (uint32_t j = 0; j < i; ++j) {
                free_decoded_image(&images[j]);
            }
            return result;
        }
        if (images[i].width != images[0].width || images[i].height != images[0].height) {
            LOG_ERROR(TEXTURE_LOG_CATEGORY, "Array layer '%s' size does not match layer 0",
                      paths[i]);
            for (uint32_t j = 0; j <= i; ++j) {
                free_decoded_image(&images[j]);
            }
            return RESULT_ERROR_INVALID_ARGUMENT;
        }
    }

    const TextureParams effective = (params != nullptr) ? *params : texture_params_default();
    const int            width = images[0].width;
    const int            height = images[0].height;

    GLuint gl_texture = 0;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &gl_texture);
    const GLsizei levels =
        effective.generate_mipmaps ? mip_level_count((uint32_t)width, (uint32_t)height) : 1;
    glTextureStorage3D(gl_texture, levels, GL_RGBA8, width, height, (GLsizei)layer_count);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (uint32_t i = 0; i < layer_count; ++i) {
        glTextureSubImage3D(gl_texture, 0, 0, 0, (GLint)i, width, height, 1, GL_RGBA,
                             GL_UNSIGNED_BYTE, images[i].pixels);
        free_decoded_image(&images[i]);
    }
    if (effective.generate_mipmaps) {
        glGenerateTextureMipmap(gl_texture);
    }
    apply_texture_params(gl_texture, &effective);

    Texture *texture = malloc(sizeof(*texture));
    if (texture == nullptr) {
        glDeleteTextures(1, &gl_texture);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    texture->gl_texture = gl_texture;
    texture->type = TEXTURE_TYPE_2D_ARRAY;
    texture->width = (uint32_t)width;
    texture->height = (uint32_t)height;
    texture->ref_count = 1;
    texture->source_path[0] = '\0'; /* texture_reload() only supports TEXTURE_TYPE_2D */
    texture->params = effective;

    cache_insert(key, texture);
    LOG_INFO(TEXTURE_LOG_CATEGORY, "Loaded texture array '%s' (%u layers)", key, layer_count);

    *out_texture = texture;
    return RESULT_OK;
}

Result texture_load_cubemap(const char *const paths[6], const TextureParams *params,
                             Texture **out_texture) {
    if (paths == nullptr || out_texture == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return RESULT_ERROR_NOT_INITIALIZED;
    }
    for (int i = 0; i < 6; ++i) {
        if (paths[i] == nullptr) {
            return RESULT_ERROR_INVALID_ARGUMENT;
        }
    }

    char key[TEXTURE_KEY_CAPACITY];
    build_joined_key(paths, 6, key, sizeof(key));

    Texture *cached = cache_find(key);
    if (cached != nullptr) {
        cached->ref_count += 1;
        *out_texture = cached;
        return RESULT_OK;
    }

    DecodedImage images[6];
    for (int i = 0; i < 6; ++i) {
        const Result result = decode_image_from_path(paths[i], &images[i]);
        if (result != RESULT_OK) {
            for (int j = 0; j < i; ++j) {
                free_decoded_image(&images[j]);
            }
            return result;
        }
        if (images[i].width != images[i].height ||
            (i > 0 && images[i].width != images[0].width)) {
            LOG_ERROR(TEXTURE_LOG_CATEGORY,
                      "Cubemap face '%s' must be square and match the other faces", paths[i]);
            for (int j = 0; j <= i; ++j) {
                free_decoded_image(&images[j]);
            }
            return RESULT_ERROR_INVALID_ARGUMENT;
        }
    }

    const TextureParams effective = (params != nullptr) ? *params : texture_params_default();
    const int            size = images[0].width;

    GLuint gl_texture = 0;
    glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &gl_texture);
    const GLsizei levels =
        effective.generate_mipmaps ? mip_level_count((uint32_t)size, (uint32_t)size) : 1;
    glTextureStorage2D(gl_texture, levels, GL_RGBA8, size, size);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 0; i < 6; ++i) {
        glTextureSubImage3D(gl_texture, 0, 0, 0, i, size, size, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                             images[i].pixels);
        free_decoded_image(&images[i]);
    }
    if (effective.generate_mipmaps) {
        glGenerateTextureMipmap(gl_texture);
    }
    apply_texture_params(gl_texture, &effective);

    Texture *texture = malloc(sizeof(*texture));
    if (texture == nullptr) {
        glDeleteTextures(1, &gl_texture);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    texture->gl_texture = gl_texture;
    texture->type = TEXTURE_TYPE_CUBEMAP;
    texture->width = (uint32_t)size;
    texture->height = (uint32_t)size;
    texture->ref_count = 1;
    texture->source_path[0] = '\0'; /* texture_reload() only supports TEXTURE_TYPE_2D */
    texture->params = effective;

    cache_insert(key, texture);
    LOG_INFO(TEXTURE_LOG_CATEGORY, "Loaded cubemap '%s' (%dx%d)", key, size, size);

    *out_texture = texture;
    return RESULT_OK;
}

Result texture_create_from_pixels(uint32_t width, uint32_t height, TextureFormat format,
                                   const void *pixels, const TextureParams *params,
                                   Texture **out_texture) {
    if (width == 0 || height == 0 || pixels == nullptr || out_texture == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return RESULT_ERROR_NOT_INITIALIZED;
    }

    const TextureParams  effective = (params != nullptr) ? *params : texture_params_default();
    const GlFormatMapping mapping = gl_format_for(format);

    GLuint gl_texture = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &gl_texture);
    const GLsizei levels = effective.generate_mipmaps ? mip_level_count(width, height) : 1;
    glTextureStorage2D(gl_texture, levels, mapping.internal_format, (GLsizei)width,
                        (GLsizei)height);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTextureSubImage2D(gl_texture, 0, 0, 0, (GLsizei)width, (GLsizei)height, mapping.data_format,
                         mapping.data_type, pixels);
    if (effective.generate_mipmaps) {
        glGenerateTextureMipmap(gl_texture);
    }
    apply_texture_params(gl_texture, &effective);

    Texture *texture = malloc(sizeof(*texture));
    if (texture == nullptr) {
        glDeleteTextures(1, &gl_texture);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    texture->gl_texture = gl_texture;
    texture->type = TEXTURE_TYPE_2D;
    texture->width = width;
    texture->height = height;
    texture->ref_count = 1;
    /* Empty source_path: texture_reload() requires one and returns
     * RESULT_ERROR_INVALID_STATE for a procedurally-created texture. */
    texture->source_path[0] = '\0';
    texture->params = effective;

    /* Not cached: there is no source path to key it on. */
    *out_texture = texture;
    return RESULT_OK;
}

void texture_retain(Texture *texture) {
    ASSERT(texture != nullptr);
    if (texture != nullptr) {
        texture->ref_count += 1;
    }
}

void texture_release(Texture *texture) {
    ASSERT(texture != nullptr);
    if (texture == nullptr) {
        return;
    }
    ASSERT_MSG(texture->ref_count > 0, "texture_release called more times than retained");
    if (texture->ref_count == 0) {
        return;
    }

    texture->ref_count -= 1;
    if (texture->ref_count > 0) {
        return;
    }

    for (uint32_t i = 0; i < g_cache_count; ++i) {
        if (g_cache[i].texture == texture) {
            g_cache[i] = g_cache[g_cache_count - 1];
            g_cache_count -= 1;
            break;
        }
    }

    glDeleteTextures(1, &texture->gl_texture);
    free(texture);
}

uint32_t texture_get_gl_handle(const Texture *texture) {
    ASSERT(texture != nullptr);
    return texture != nullptr ? texture->gl_texture : 0;
}

TextureType texture_get_type(const Texture *texture) {
    ASSERT(texture != nullptr);
    return texture != nullptr ? texture->type : TEXTURE_TYPE_2D;
}

uint32_t texture_get_width(const Texture *texture) {
    ASSERT(texture != nullptr);
    return texture != nullptr ? texture->width : 0;
}

uint32_t texture_get_height(const Texture *texture) {
    ASSERT(texture != nullptr);
    return texture != nullptr ? texture->height : 0;
}

void texture_bind(const Texture *texture, uint32_t unit) {
    ASSERT(texture != nullptr);
    if (texture == nullptr) {
        return;
    }
    glBindTextureUnit(unit, texture->gl_texture);
}

Result sampler_create(const TextureParams *params, Sampler **out_sampler) {
    if (out_sampler == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    const TextureParams effective = (params != nullptr) ? *params : texture_params_default();

    GLuint gl_sampler = 0;
    glCreateSamplers(1, &gl_sampler);
    apply_sampler_params(gl_sampler, &effective);

    Sampler *sampler = malloc(sizeof(*sampler));
    if (sampler == nullptr) {
        glDeleteSamplers(1, &gl_sampler);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    sampler->gl_sampler = gl_sampler;

    *out_sampler = sampler;
    return RESULT_OK;
}

void sampler_destroy(Sampler *sampler) {
    if (sampler == nullptr) {
        return;
    }
    glDeleteSamplers(1, &sampler->gl_sampler);
    free(sampler);
}

void sampler_bind(const Sampler *sampler, uint32_t unit) {
    ASSERT(sampler != nullptr);
    if (sampler == nullptr) {
        return;
    }
    glBindSampler(unit, sampler->gl_sampler);
}

void sampler_unbind_unit(uint32_t unit) {
    glBindSampler(unit, 0);
}
