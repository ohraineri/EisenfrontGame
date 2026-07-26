/*
 * Lighting - directional, point and spot light data, uploaded once per
 * frame into a uniform block every lit shader binds. Each light kind is
 * independently optional/settable, so a scene with only a directional
 * light pays for exactly that (the point/spot arrays are simply left at
 * count 0 - still allocated in the UBO, but the shader loop over them
 * runs zero iterations).
 *
 * Dependencies: Core, Graphics Context, Buffers, cglm (publicly, like
 * Camera and Scene).
 */
#ifndef EISENFRONT_LIGHTING_H
#define EISENFRONT_LIGHTING_H

#include "buffers.h"
#include "core.h"
#include "graphics_context.h"

#include <cglm/cglm.h>

#define LIGHTING_MAX_POINT_LIGHTS 8u
#define LIGHTING_MAX_SPOT_LIGHTS 4u
#define LIGHTING_UBO_BINDING 2u /* 0: reserved for a future per-frame/camera UBO; 1: Material (see material.h) */

typedef struct DirectionalLight {
    vec3  direction; /* normalized; points FROM the light TOWARD the scene */
    vec3  color;
    float intensity;
} DirectionalLight;

typedef struct PointLight {
    vec3  position;
    vec3  color;
    float intensity;
    float radius; /* distance at which attenuation reaches ~0 */
} PointLight;

typedef struct SpotLight {
    vec3  position;
    vec3  direction; /* normalized */
    vec3  color;
    float intensity;
    float radius;
    float inner_cone_cos; /* cos(half-angle) of the full-intensity inner cone */
    float outer_cone_cos; /* cos(half-angle) of the falls-to-zero outer cone; must be < inner_cone_cos */
} SpotLight;

typedef struct LightingEnvironment LightingEnvironment;

ENGINE_API Result lighting_environment_create(LightingEnvironment **out_environment);
ENGINE_API void   lighting_environment_destroy(LightingEnvironment *environment);

/* nullptr disables the directional light. */
ENGINE_API void lighting_set_directional(LightingEnvironment *environment,
                                          const DirectionalLight *light);

ENGINE_API Result lighting_set_point_light(LightingEnvironment *environment, uint32_t index,
                                            const PointLight *light);
ENGINE_API void   lighting_set_point_light_count(LightingEnvironment *environment, uint32_t count);

ENGINE_API Result lighting_set_spot_light(LightingEnvironment *environment, uint32_t index,
                                           const SpotLight *light);
ENGINE_API void   lighting_set_spot_light_count(LightingEnvironment *environment, uint32_t count);

/* Packs the current CPU-side state into the GPU uniform block. Call
 * after any lighting_set_*() changes and before lighting_bind(). */
ENGINE_API void lighting_upload(LightingEnvironment *environment);
/* Binds the lighting UBO to LIGHTING_UBO_BINDING, which every lit
 * shader declares as `layout(std140, binding = 2) uniform Lighting { ... }`. */
ENGINE_API void lighting_bind(const LightingEnvironment *environment);

#endif /* EISENFRONT_LIGHTING_H */
