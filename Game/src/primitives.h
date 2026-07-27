/*
 * World-space primitive mesh builders. Every vertex is already baked
 * into world position at build time (see lit.vert's file header
 * comment for why) - these are not reusable unit-cube/unit-quad meshes
 * transformed by a per-object uniform.
 */
#ifndef OUTPOST_PRIMITIVES_H
#define OUTPOST_PRIMITIVES_H

#include "eisenfront/mesh.h"

#include <cglm/cglm.h>

/* Axis-aligned box spanning center +/- half_extents. uv_scale is
 * texture repeats per world unit on each face. */
Result primitive_create_box(vec3 center, vec3 half_extents, float uv_scale, Mesh **out_mesh);

/* Flat horizontal quad on the XZ plane at y = center[1], spanning
 * center.xz +/- {half_width, half_depth}. */
Result primitive_create_ground_plane(vec3 center, float half_width, float half_depth,
                                      float uv_scale, Mesh **out_mesh);

#endif /* OUTPOST_PRIMITIVES_H */
