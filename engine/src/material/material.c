#include "eisenfront/material.h"

#include <stdlib.h>
#include <string.h>

/* std140 layout: a vec4 block (base_color), another vec4 block
 * (emissive_factor.xyz + 1 float of padding), then three scalars
 * (metallic/roughness/ao) followed by one float of padding to round the
 * struct up to a multiple of 16 bytes, as std140 requires. */
typedef struct MaterialParamsGpu {
    float base_color[4];
    float emissive_factor[4];
    float metallic_factor;
    float roughness_factor;
    float ao_strength;
    float _pad0;
} MaterialParamsGpu;

struct Material {
    ShaderProgram *shader;
    Texture       *diffuse_texture;
    Texture       *normal_texture;
    Texture       *metallic_texture;
    Texture       *roughness_texture;
    Texture       *ao_texture;
    Texture       *emissive_texture;
    GpuBuffer     *params_ubo;
    MaterialParams params; /* CPU mirror of params_ubo's contents, read by material_instance_create() */
    uint32_t       ref_count;
};

struct MaterialInstance {
    Material  *base;
    Texture   *diffuse_texture;
    Texture   *normal_texture;
    Texture   *metallic_texture;
    Texture   *roughness_texture;
    Texture   *ao_texture;
    Texture   *emissive_texture;
    GpuBuffer *params_ubo;
    uint32_t   ref_count;
};

static bool     g_initialized = false;
static Texture *g_fallback_white = nullptr;
static Texture *g_fallback_normal = nullptr;

Result material_system_init(void) {
    if (g_initialized) {
        return RESULT_ERROR_ALREADY_INITIALIZED;
    }

    TextureParams fallback_params = texture_params_default();
    fallback_params.generate_mipmaps = false;
    fallback_params.wrap_s = TEXTURE_WRAP_REPEAT;
    fallback_params.wrap_t = TEXTURE_WRAP_REPEAT;

    const uint8_t white_pixel[4] = {255, 255, 255, 255};
    Result        result = texture_create_from_pixels(1, 1, TEXTURE_FORMAT_RGBA8, white_pixel,
                                                        &fallback_params, &g_fallback_white);
    if (result != RESULT_OK) {
        return result;
    }

    /* (0.5, 0.5, 1.0) is the standard tangent-space "no bump" normal,
     * encoded from [-1,1] into the [0,1] texture range. */
    const uint8_t normal_pixel[4] = {128, 128, 255, 255};
    result = texture_create_from_pixels(1, 1, TEXTURE_FORMAT_RGBA8, normal_pixel, &fallback_params,
                                         &g_fallback_normal);
    if (result != RESULT_OK) {
        texture_release(g_fallback_white);
        g_fallback_white = nullptr;
        return result;
    }

    g_initialized = true;
    return RESULT_OK;
}

void material_system_shutdown(void) {
    if (!g_initialized) {
        return;
    }
    texture_release(g_fallback_white);
    texture_release(g_fallback_normal);
    g_fallback_white = nullptr;
    g_fallback_normal = nullptr;
    g_initialized = false;
}

MaterialParams material_params_default(void) {
    return (MaterialParams){
        .base_color = {1.0f, 1.0f, 1.0f, 1.0f},
        .emissive_factor = {0.0f, 0.0f, 0.0f},
        .metallic_factor = 0.0f,
        .roughness_factor = 1.0f,
        .ao_strength = 1.0f,
    };
}

static void pack_params(const MaterialParams *params, MaterialParamsGpu *out_gpu) {
    out_gpu->base_color[0] = params->base_color[0];
    out_gpu->base_color[1] = params->base_color[1];
    out_gpu->base_color[2] = params->base_color[2];
    out_gpu->base_color[3] = params->base_color[3];
    out_gpu->emissive_factor[0] = params->emissive_factor[0];
    out_gpu->emissive_factor[1] = params->emissive_factor[1];
    out_gpu->emissive_factor[2] = params->emissive_factor[2];
    out_gpu->emissive_factor[3] = 0.0f;
    out_gpu->metallic_factor = params->metallic_factor;
    out_gpu->roughness_factor = params->roughness_factor;
    out_gpu->ao_strength = params->ao_strength;
    out_gpu->_pad0 = 0.0f;
}

static Result create_params_ubo(const MaterialParams *params, GpuBuffer **out_ubo) {
    MaterialParamsGpu gpu_params;
    pack_params(params, &gpu_params);

    const BufferDesc desc = {
        .type = BUFFER_TYPE_UNIFORM,
        .usage = BUFFER_USAGE_DYNAMIC,
        .initial_data = &gpu_params,
        .size_bytes = sizeof(gpu_params),
    };
    return gpu_buffer_create(&desc, out_ubo);
}

static void retain_texture_or_null(Texture *texture) {
    if (texture != nullptr) {
        texture_retain(texture);
    }
}

static void release_texture_or_null(Texture *texture) {
    if (texture != nullptr) {
        texture_release(texture);
    }
}

Result material_create(const MaterialDesc *desc, Material **out_material) {
    if (desc == nullptr || desc->shader == nullptr || out_material == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return RESULT_ERROR_NOT_INITIALIZED;
    }

    GpuBuffer *ubo = nullptr;
    Result     result = create_params_ubo(&desc->params, &ubo);
    if (result != RESULT_OK) {
        return result;
    }

    Material *material = malloc(sizeof(*material));
    if (material == nullptr) {
        gpu_buffer_destroy(ubo);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    shader_program_retain(desc->shader);
    material->shader = desc->shader;
    material->diffuse_texture = desc->textures.diffuse;
    material->normal_texture = desc->textures.normal;
    material->metallic_texture = desc->textures.metallic;
    material->roughness_texture = desc->textures.roughness;
    material->ao_texture = desc->textures.ao;
    material->emissive_texture = desc->textures.emissive;
    retain_texture_or_null(material->diffuse_texture);
    retain_texture_or_null(material->normal_texture);
    retain_texture_or_null(material->metallic_texture);
    retain_texture_or_null(material->roughness_texture);
    retain_texture_or_null(material->ao_texture);
    retain_texture_or_null(material->emissive_texture);
    material->params_ubo = ubo;
    material->params = desc->params;
    material->ref_count = 1;

    *out_material = material;
    return RESULT_OK;
}

void material_retain(Material *material) {
    ASSERT(material != nullptr);
    if (material != nullptr) {
        material->ref_count += 1;
    }
}

void material_release(Material *material) {
    ASSERT(material != nullptr);
    if (material == nullptr) {
        return;
    }
    ASSERT_MSG(material->ref_count > 0, "material_release called more times than retained");
    if (material->ref_count == 0) {
        return;
    }
    material->ref_count -= 1;
    if (material->ref_count > 0) {
        return;
    }

    release_texture_or_null(material->diffuse_texture);
    release_texture_or_null(material->normal_texture);
    release_texture_or_null(material->metallic_texture);
    release_texture_or_null(material->roughness_texture);
    release_texture_or_null(material->ao_texture);
    release_texture_or_null(material->emissive_texture);
    gpu_buffer_destroy(material->params_ubo);
    shader_program_release(material->shader);
    free(material);
}

void material_set_textures(Material *material, const MaterialTextures *textures) {
    ASSERT(material != nullptr);
    ASSERT(textures != nullptr);
    if (material == nullptr || textures == nullptr) {
        return;
    }

    retain_texture_or_null(textures->diffuse);
    retain_texture_or_null(textures->normal);
    retain_texture_or_null(textures->metallic);
    retain_texture_or_null(textures->roughness);
    retain_texture_or_null(textures->ao);
    retain_texture_or_null(textures->emissive);

    release_texture_or_null(material->diffuse_texture);
    release_texture_or_null(material->normal_texture);
    release_texture_or_null(material->metallic_texture);
    release_texture_or_null(material->roughness_texture);
    release_texture_or_null(material->ao_texture);
    release_texture_or_null(material->emissive_texture);

    material->diffuse_texture = textures->diffuse;
    material->normal_texture = textures->normal;
    material->metallic_texture = textures->metallic;
    material->roughness_texture = textures->roughness;
    material->ao_texture = textures->ao;
    material->emissive_texture = textures->emissive;
}

void material_set_params(Material *material, const MaterialParams *params) {
    ASSERT(material != nullptr);
    ASSERT(params != nullptr);
    if (material == nullptr || params == nullptr) {
        return;
    }
    MaterialParamsGpu gpu_params;
    pack_params(params, &gpu_params);
    gpu_buffer_update(material->params_ubo, 0, &gpu_params, sizeof(gpu_params));
    material->params = *params;
}

static void bind_textures_and_ubo(Texture *diffuse, Texture *normal, Texture *metallic,
                                   Texture *roughness, Texture *ao, Texture *emissive,
                                   GpuBuffer *ubo) {
    texture_bind(diffuse != nullptr ? diffuse : g_fallback_white, MATERIAL_TEXTURE_UNIT_DIFFUSE);
    texture_bind(normal != nullptr ? normal : g_fallback_normal, MATERIAL_TEXTURE_UNIT_NORMAL);
    texture_bind(metallic != nullptr ? metallic : g_fallback_white, MATERIAL_TEXTURE_UNIT_METALLIC);
    texture_bind(roughness != nullptr ? roughness : g_fallback_white,
                 MATERIAL_TEXTURE_UNIT_ROUGHNESS);
    texture_bind(ao != nullptr ? ao : g_fallback_white, MATERIAL_TEXTURE_UNIT_AO);
    texture_bind(emissive != nullptr ? emissive : g_fallback_white, MATERIAL_TEXTURE_UNIT_EMISSIVE);
    gpu_buffer_bind_indexed(ubo, MATERIAL_UBO_BINDING);
}

void material_bind(const Material *material) {
    ASSERT(material != nullptr);
    if (material == nullptr) {
        return;
    }
    glUseProgram(shader_program_get_gl_handle(material->shader));
    bind_textures_and_ubo(material->diffuse_texture, material->normal_texture,
                           material->metallic_texture, material->roughness_texture,
                           material->ao_texture, material->emissive_texture, material->params_ubo);
}

Result material_instance_create(Material *base, MaterialInstance **out_instance) {
    if (base == nullptr || out_instance == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    MaterialInstance *instance = malloc(sizeof(*instance));
    if (instance == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    material_retain(base);
    instance->base = base;
    instance->diffuse_texture = base->diffuse_texture;
    instance->normal_texture = base->normal_texture;
    instance->metallic_texture = base->metallic_texture;
    instance->roughness_texture = base->roughness_texture;
    instance->ao_texture = base->ao_texture;
    instance->emissive_texture = base->emissive_texture;
    retain_texture_or_null(instance->diffuse_texture);
    retain_texture_or_null(instance->normal_texture);
    retain_texture_or_null(instance->metallic_texture);
    retain_texture_or_null(instance->roughness_texture);
    retain_texture_or_null(instance->ao_texture);
    retain_texture_or_null(instance->emissive_texture);

    /* Instances start as a copy of the base's current parameters, in a
     * separate UBO so material_instance_set_params() can diverge from
     * the base without touching it. */
    Result result = create_params_ubo(&base->params, &instance->params_ubo);
    if (result != RESULT_OK) {
        release_texture_or_null(instance->diffuse_texture);
        release_texture_or_null(instance->normal_texture);
        release_texture_or_null(instance->metallic_texture);
        release_texture_or_null(instance->roughness_texture);
        release_texture_or_null(instance->ao_texture);
        release_texture_or_null(instance->emissive_texture);
        material_release(base);
        free(instance);
        return result;
    }
    instance->ref_count = 1;

    *out_instance = instance;
    return RESULT_OK;
}

void material_instance_retain(MaterialInstance *instance) {
    ASSERT(instance != nullptr);
    if (instance != nullptr) {
        instance->ref_count += 1;
    }
}

void material_instance_release(MaterialInstance *instance) {
    ASSERT(instance != nullptr);
    if (instance == nullptr) {
        return;
    }
    ASSERT_MSG(instance->ref_count > 0,
               "material_instance_release called more times than retained");
    if (instance->ref_count == 0) {
        return;
    }
    instance->ref_count -= 1;
    if (instance->ref_count > 0) {
        return;
    }

    release_texture_or_null(instance->diffuse_texture);
    release_texture_or_null(instance->normal_texture);
    release_texture_or_null(instance->metallic_texture);
    release_texture_or_null(instance->roughness_texture);
    release_texture_or_null(instance->ao_texture);
    release_texture_or_null(instance->emissive_texture);
    gpu_buffer_destroy(instance->params_ubo);
    material_release(instance->base);
    free(instance);
}

void material_instance_set_textures(MaterialInstance *instance, const MaterialTextures *textures) {
    ASSERT(instance != nullptr);
    ASSERT(textures != nullptr);
    if (instance == nullptr || textures == nullptr) {
        return;
    }

    retain_texture_or_null(textures->diffuse);
    retain_texture_or_null(textures->normal);
    retain_texture_or_null(textures->metallic);
    retain_texture_or_null(textures->roughness);
    retain_texture_or_null(textures->ao);
    retain_texture_or_null(textures->emissive);

    release_texture_or_null(instance->diffuse_texture);
    release_texture_or_null(instance->normal_texture);
    release_texture_or_null(instance->metallic_texture);
    release_texture_or_null(instance->roughness_texture);
    release_texture_or_null(instance->ao_texture);
    release_texture_or_null(instance->emissive_texture);

    instance->diffuse_texture = textures->diffuse;
    instance->normal_texture = textures->normal;
    instance->metallic_texture = textures->metallic;
    instance->roughness_texture = textures->roughness;
    instance->ao_texture = textures->ao;
    instance->emissive_texture = textures->emissive;
}

void material_instance_set_params(MaterialInstance *instance, const MaterialParams *params) {
    ASSERT(instance != nullptr);
    ASSERT(params != nullptr);
    if (instance == nullptr || params == nullptr) {
        return;
    }
    MaterialParamsGpu gpu_params;
    pack_params(params, &gpu_params);
    gpu_buffer_update(instance->params_ubo, 0, &gpu_params, sizeof(gpu_params));
}

void material_instance_bind(const MaterialInstance *instance) {
    ASSERT(instance != nullptr);
    if (instance == nullptr) {
        return;
    }
    glUseProgram(shader_program_get_gl_handle(instance->base->shader));
    bind_textures_and_ubo(instance->diffuse_texture, instance->normal_texture,
                           instance->metallic_texture, instance->roughness_texture,
                           instance->ao_texture, instance->emissive_texture, instance->params_ubo);
}
