/*
 * Camera - perspective, orthographic and FPS-style cameras: view and
 * projection matrices, frustum extraction, movement and rotation.
 *
 * Unlike Window/Input (which hide SDL) or Texture (which hides
 * stb_image), this header deliberately exposes cglm's vec3/mat4: Math
 * is the one dependency meant to be a shared vocabulary across the
 * whole engine (Camera today; Scene, ECS, Physics later), not an
 * implementation detail scoped to one module.
 *
 * Camera is a plain value type, not an opaque handle: it owns no OS or
 * GPU resource (no file, no GL object, nothing to leak), so - unlike
 * every other module in this engine - there is no create/destroy pair
 * and no heap allocation. Construct one with camera_create_perspective()
 * / camera_create_orthographic(), then treat it like any other struct:
 * copy it, embed it in a Scene node, put many of them in an array.
 *
 * Coordinate conventions: right-handed, Y-up world space (matching
 * OpenGL's default). Yaw rotates around the world Y axis; pitch rotates
 * around the camera's local right axis. At yaw = 0, pitch = 0 the
 * camera looks down +X. All angles are radians.
 *
 * Dependencies: Core and cglm (publicly).
 */
#ifndef EISENFRONT_CAMERA_H
#define EISENFRONT_CAMERA_H

#include "core.h"

#include <cglm/cglm.h>

typedef enum CameraProjectionType {
    CAMERA_PROJECTION_PERSPECTIVE = 0,
    CAMERA_PROJECTION_ORTHOGRAPHIC,
} CameraProjectionType;

typedef struct Camera {
    vec3 position;
    float yaw_radians;
    float pitch_radians;

    CameraProjectionType projection_type;
    float fov_y_radians;     /* perspective only */
    float ortho_half_height; /* orthographic only: half the vertical view size, world units */
    float aspect_ratio;      /* width / height, both projection types */
    float near_plane;
    float far_plane;
} Camera;

ENGINE_API Camera camera_create_perspective(vec3 position, float fov_y_radians,
                                             float aspect_ratio, float near_plane,
                                             float far_plane);
ENGINE_API Camera camera_create_orthographic(vec3 position, float half_height, float aspect_ratio,
                                              float near_plane, float far_plane);

ENGINE_API void camera_set_aspect_ratio(Camera *camera, float aspect_ratio);

/* Unit forward/right/up vectors derived from yaw/pitch - see camera.c
 * for the derivation; exposed because view-matrix construction, frustum
 * extraction and camera_move_local() all share it. */
ENGINE_API void camera_get_forward(const Camera *camera, vec3 out_forward);
ENGINE_API void camera_get_right(const Camera *camera, vec3 out_right);
ENGINE_API void camera_get_up(const Camera *camera, vec3 out_up);

ENGINE_API void camera_get_view_matrix(const Camera *camera, mat4 out_view);
ENGINE_API void camera_get_projection_matrix(const Camera *camera, mat4 out_projection);
/* projection * view, in that order - what a vertex shader's clip-space
 * transform actually needs. */
ENGINE_API void camera_get_view_projection_matrix(const Camera *camera, mat4 out_view_projection);

/* offset.x = local right, offset.y = world up (not local up - see
 * camera.c), offset.z = local forward. A typical FPS-camera update
 * scales offset by (move speed * delta time) before calling this. */
ENGINE_API void camera_move_local(Camera *camera, vec3 offset);
/* Adds to yaw/pitch and clamps pitch just short of +-90 degrees so the
 * view can never flip past straight up/down. */
ENGINE_API void camera_rotate(Camera *camera, float delta_yaw_radians, float delta_pitch_radians);

typedef struct Frustum {
    /* ax + by + cz + d = 0, (a,b,c) unit length, positive-side = inside.
     * Order: left, right, bottom, top, near, far. */
    vec4 planes[6];
} Frustum;

ENGINE_API Frustum camera_get_frustum(const Camera *camera);
ENGINE_API bool     frustum_contains_sphere(const Frustum *frustum, vec3 center, float radius);
ENGINE_API bool     frustum_contains_point(const Frustum *frustum, vec3 point);

#endif /* EISENFRONT_CAMERA_H */
