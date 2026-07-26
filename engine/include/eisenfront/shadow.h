/*
 * Shadow - a single directional-light shadow map: render scene depth
 * from the light's point of view into a depth-only Framebuffer, then a
 * later lit shader compares each fragment's depth in that same
 * light-space against the stored value to decide if it is occluded.
 * Producing that comparison result is this module's job up through the
 * depth texture; consuming it (the shadow test inside a lighting
 * shader) belongs to whatever shader the game supplies, since this
 * engine does not yet own a full lit shader (see material.h - Material
 * covers PBR parameters and binding, not a specific lighting shader).
 *
 * Scoped to one directional light, no cascades and no point/spot light
 * (cubemap/perspective) shadows - the same kind of explicit, documented
 * scope boundary as Renderer deferring lighting or the Asset Manager's
 * model import deferring materials.
 *
 * Dependencies: Core, Graphics Context, Framebuffer, cglm.
 */
#ifndef EISENFRONT_SHADOW_H
#define EISENFRONT_SHADOW_H

#include "core.h"
#include "framebuffer.h"
#include "graphics_context.h"

#include <cglm/cglm.h>

typedef struct ShadowMap ShadowMap;

ENGINE_API Result shadow_map_create(uint32_t resolution, ShadowMap **out_shadow_map);
ENGINE_API void   shadow_map_destroy(ShadowMap *shadow_map);

/* Computes the light-space view-projection matrix that tightly bounds a
 * sphere of scene_radius centered at scene_center (typically the
 * camera frustum's or whole scene's bounding sphere), as seen from a
 * light traveling in light_direction (same convention as
 * DirectionalLight.direction in lighting.h: points FROM the light
 * TOWARD the scene). Call whenever the light direction or the bounded
 * region changes; the result is cached until the next call. */
ENGINE_API void shadow_map_set_light(ShadowMap *shadow_map, vec3 light_direction, vec3 scene_center,
                                      float scene_radius);
ENGINE_API void shadow_map_get_light_space_matrix(const ShadowMap *shadow_map, mat4 out_matrix);

/* Binds the shadow map's framebuffer and clears its depth buffer;
 * follow with one or more shadow_map_draw() calls, then bind a
 * different framebuffer (e.g. framebuffer_bind_default()) to resume
 * normal rendering. */
ENGINE_API void shadow_map_begin(ShadowMap *shadow_map);
/* Renders vertex_array's position attribute (location 0) through the
 * cached light-space matrix and model_matrix - a depth-only pass, using
 * ShadowMap's own minimal embedded shader, so no shader binding is
 * required from the caller. index_count > 0 selects an indexed draw
 * (vertex_array's bound element buffer supplies the indices);
 * otherwise vertex_count vertices are drawn directly. */
ENGINE_API void shadow_map_draw(const ShadowMap *shadow_map, mat4 model_matrix,
                                 uint32_t vertex_array, uint32_t vertex_count,
                                 uint32_t index_count);

/* For a lit shader to sample (with its own comparison sampler) once
 * bound to a texture unit of the caller's choosing. */
ENGINE_API uint32_t shadow_map_get_depth_texture(const ShadowMap *shadow_map);
ENGINE_API uint32_t shadow_map_get_resolution(const ShadowMap *shadow_map);

#endif /* EISENFRONT_SHADOW_H */
