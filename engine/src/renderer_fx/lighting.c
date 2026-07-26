#include "eisenfront/lighting.h"

#include <stdlib.h>
#include <string.h>

/* std140 layout notes: every array element below is exactly one or more
 * vec4 (16 bytes), so array stride is unambiguous (std140 always rounds
 * array element stride up to a multiple of 16 anyway); each light packs
 * a scalar into an otherwise-wasted vec4 lane (radius/intensity/cone
 * cosines) rather than spending a whole separate vec4 on it. */
typedef struct DirectionalLightGpu {
    float direction[4]; /* xyz, w unused */
    float color[4];     /* rgb, intensity in w */
} DirectionalLightGpu;

typedef struct PointLightGpu {
    float position[4]; /* xyz, radius in w */
    float color[4];     /* rgb, intensity in w */
} PointLightGpu;

typedef struct SpotLightGpu {
    float position[4];  /* xyz, radius in w */
    float direction[4]; /* xyz, inner_cone_cos in w */
    float color[4];      /* rgb, intensity in w */
    float cone[4];        /* outer_cone_cos, then unused padding */
} SpotLightGpu;

typedef struct LightingGpuData {
    DirectionalLightGpu directional;
    int32_t             has_directional;
    int32_t             point_light_count;
    int32_t             spot_light_count;
    int32_t             _pad0;
    PointLightGpu        point_lights[LIGHTING_MAX_POINT_LIGHTS];
    SpotLightGpu          spot_lights[LIGHTING_MAX_SPOT_LIGHTS];
} LightingGpuData;

struct LightingEnvironment {
    bool             has_directional;
    DirectionalLight directional;
    uint32_t         point_light_count;
    PointLight       point_lights[LIGHTING_MAX_POINT_LIGHTS];
    uint32_t         spot_light_count;
    SpotLight        spot_lights[LIGHTING_MAX_SPOT_LIGHTS];
    GpuBuffer       *ubo;
};

Result lighting_environment_create(LightingEnvironment **out_environment) {
    if (out_environment == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    LightingEnvironment *environment = calloc(1, sizeof(*environment));
    if (environment == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    const BufferDesc desc = {
        .type = BUFFER_TYPE_UNIFORM,
        .usage = BUFFER_USAGE_DYNAMIC,
        .size_bytes = sizeof(LightingGpuData),
    };
    const Result result = gpu_buffer_create(&desc, &environment->ubo);
    if (result != RESULT_OK) {
        free(environment);
        return result;
    }

    *out_environment = environment;
    return RESULT_OK;
}

void lighting_environment_destroy(LightingEnvironment *environment) {
    if (environment == nullptr) {
        return;
    }
    gpu_buffer_destroy(environment->ubo);
    free(environment);
}

void lighting_set_directional(LightingEnvironment *environment, const DirectionalLight *light) {
    ASSERT(environment != nullptr);
    if (environment == nullptr) {
        return;
    }
    environment->has_directional = (light != nullptr);
    if (light != nullptr) {
        environment->directional = *light;
    }
}

Result lighting_set_point_light(LightingEnvironment *environment, uint32_t index,
                                 const PointLight *light) {
    if (environment == nullptr || light == nullptr || index >= LIGHTING_MAX_POINT_LIGHTS) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    environment->point_lights[index] = *light;
    return RESULT_OK;
}

void lighting_set_point_light_count(LightingEnvironment *environment, uint32_t count) {
    ASSERT(environment != nullptr);
    if (environment == nullptr) {
        return;
    }
    environment->point_light_count =
        (count < LIGHTING_MAX_POINT_LIGHTS) ? count : LIGHTING_MAX_POINT_LIGHTS;
}

Result lighting_set_spot_light(LightingEnvironment *environment, uint32_t index,
                                const SpotLight *light) {
    if (environment == nullptr || light == nullptr || index >= LIGHTING_MAX_SPOT_LIGHTS) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    environment->spot_lights[index] = *light;
    return RESULT_OK;
}

void lighting_set_spot_light_count(LightingEnvironment *environment, uint32_t count) {
    ASSERT(environment != nullptr);
    if (environment == nullptr) {
        return;
    }
    environment->spot_light_count =
        (count < LIGHTING_MAX_SPOT_LIGHTS) ? count : LIGHTING_MAX_SPOT_LIGHTS;
}

void lighting_upload(LightingEnvironment *environment) {
    ASSERT(environment != nullptr);
    if (environment == nullptr) {
        return;
    }

    LightingGpuData gpu_data;
    memset(&gpu_data, 0, sizeof(gpu_data));

    gpu_data.has_directional = environment->has_directional ? 1 : 0;
    if (environment->has_directional) {
        gpu_data.directional.direction[0] = environment->directional.direction[0];
        gpu_data.directional.direction[1] = environment->directional.direction[1];
        gpu_data.directional.direction[2] = environment->directional.direction[2];
        gpu_data.directional.color[0] = environment->directional.color[0];
        gpu_data.directional.color[1] = environment->directional.color[1];
        gpu_data.directional.color[2] = environment->directional.color[2];
        gpu_data.directional.color[3] = environment->directional.intensity;
    }

    gpu_data.point_light_count = (int32_t)environment->point_light_count;
    for (uint32_t i = 0; i < environment->point_light_count; ++i) {
        const PointLight *src = &environment->point_lights[i];
        PointLightGpu     *dst = &gpu_data.point_lights[i];
        dst->position[0] = src->position[0];
        dst->position[1] = src->position[1];
        dst->position[2] = src->position[2];
        dst->position[3] = src->radius;
        dst->color[0] = src->color[0];
        dst->color[1] = src->color[1];
        dst->color[2] = src->color[2];
        dst->color[3] = src->intensity;
    }

    gpu_data.spot_light_count = (int32_t)environment->spot_light_count;
    for (uint32_t i = 0; i < environment->spot_light_count; ++i) {
        const SpotLight *src = &environment->spot_lights[i];
        SpotLightGpu      *dst = &gpu_data.spot_lights[i];
        dst->position[0] = src->position[0];
        dst->position[1] = src->position[1];
        dst->position[2] = src->position[2];
        dst->position[3] = src->radius;
        dst->direction[0] = src->direction[0];
        dst->direction[1] = src->direction[1];
        dst->direction[2] = src->direction[2];
        dst->direction[3] = src->inner_cone_cos;
        dst->color[0] = src->color[0];
        dst->color[1] = src->color[1];
        dst->color[2] = src->color[2];
        dst->color[3] = src->intensity;
        dst->cone[0] = src->outer_cone_cos;
    }

    gpu_buffer_update(environment->ubo, 0, &gpu_data, sizeof(gpu_data));
}

void lighting_bind(const LightingEnvironment *environment) {
    ASSERT(environment != nullptr);
    if (environment == nullptr) {
        return;
    }
    gpu_buffer_bind_indexed(environment->ubo, LIGHTING_UBO_BINDING);
}
