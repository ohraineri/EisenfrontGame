#include "eisenfront/camera.h"

#include <math.h>

/* ~89 degrees: strictly less than pi/2 so cos(pitch) in
 * camera_get_forward() never reaches exactly zero. */
#define CAMERA_PITCH_LIMIT_RADIANS 1.5533430343f

Camera camera_create_perspective(vec3 position, float fov_y_radians, float aspect_ratio,
                                  float near_plane, float far_plane) {
    Camera camera;
    camera.position[0] = position[0];
    camera.position[1] = position[1];
    camera.position[2] = position[2];
    camera.yaw_radians = 0.0f;
    camera.pitch_radians = 0.0f;
    camera.projection_type = CAMERA_PROJECTION_PERSPECTIVE;
    camera.fov_y_radians = fov_y_radians;
    camera.ortho_half_height = 1.0f;
    camera.aspect_ratio = aspect_ratio;
    camera.near_plane = near_plane;
    camera.far_plane = far_plane;
    return camera;
}

Camera camera_create_orthographic(vec3 position, float half_height, float aspect_ratio,
                                   float near_plane, float far_plane) {
    Camera camera;
    camera.position[0] = position[0];
    camera.position[1] = position[1];
    camera.position[2] = position[2];
    camera.yaw_radians = 0.0f;
    camera.pitch_radians = 0.0f;
    camera.projection_type = CAMERA_PROJECTION_ORTHOGRAPHIC;
    camera.fov_y_radians = 0.0f;
    camera.ortho_half_height = half_height;
    camera.aspect_ratio = aspect_ratio;
    camera.near_plane = near_plane;
    camera.far_plane = far_plane;
    return camera;
}

void camera_set_aspect_ratio(Camera *camera, float aspect_ratio) {
    ASSERT(camera != nullptr);
    if (camera != nullptr) {
        camera->aspect_ratio = aspect_ratio;
    }
}

/*
 * Forward direction from yaw/pitch by spherical-to-Cartesian
 * conversion: pitch is the angle above the horizontal plane, yaw is the
 * angle around the vertical (Y) axis measured from +X toward +Z.
 * cos(pitch) shrinks the horizontal (X/Z) component as the camera tilts
 * toward straight up/down while sin(pitch) grows the vertical (Y)
 * component - the same construction used to convert a point on a unit
 * sphere from (yaw, pitch) angles into (x, y, z).
 */
void camera_get_forward(const Camera *camera, vec3 out_forward) {
    ASSERT(camera != nullptr);
    if (camera == nullptr) {
        return;
    }
    const float cos_pitch = cosf(camera->pitch_radians);
    out_forward[0] = cos_pitch * cosf(camera->yaw_radians);
    out_forward[1] = sinf(camera->pitch_radians);
    out_forward[2] = cos_pitch * sinf(camera->yaw_radians);
    glm_vec3_normalize(out_forward);
}

/*
 * Right = forward x world_up, normalized. Crossing "forward" with the
 * world up axis gives a vector perpendicular to both; by construction
 * that always lies in the horizontal plane, which is exactly "right"
 * for a camera that never rolls (banks) around its own forward axis.
 */
void camera_get_right(const Camera *camera, vec3 out_right) {
    ASSERT(camera != nullptr);
    if (camera == nullptr) {
        return;
    }
    vec3 forward;
    camera_get_forward(camera, forward);
    vec3 world_up = {0.0f, 1.0f, 0.0f};
    glm_vec3_cross(forward, world_up, out_right);
    glm_vec3_normalize(out_right);
}

/*
 * True up = right x forward, normalized. forward and right are already
 * perpendicular unit vectors, so their cross product is unit length
 * already too (normalized again here only as a guard against floating
 * point drift). This is the camera's actual local up, which - because
 * of how "right" was built - only equals world up when pitch is zero;
 * that is correct and expected (tilting the camera up/down tilts its
 * local up vector along with it).
 */
void camera_get_up(const Camera *camera, vec3 out_up) {
    ASSERT(camera != nullptr);
    if (camera == nullptr) {
        return;
    }
    vec3 forward;
    vec3 right;
    camera_get_forward(camera, forward);
    camera_get_right(camera, right);
    glm_vec3_cross(right, forward, out_up);
    glm_vec3_normalize(out_up);
}

/*
 * The view matrix transforms world-space points into camera space
 * (camera at the origin, looking down -Z, +Y up - OpenGL's convention).
 * glm_lookat builds exactly that from an eye position, a point to look
 * at, and an up hint; "look at" is simply "look toward eye + forward",
 * so the camera's position plus the forward vector derived above is all
 * it needs.
 */
void camera_get_view_matrix(const Camera *camera, mat4 out_view) {
    ASSERT(camera != nullptr);
    if (camera == nullptr) {
        return;
    }
    vec3 eye = {camera->position[0], camera->position[1], camera->position[2]};
    vec3 forward;
    camera_get_forward(camera, forward);
    vec3 target;
    glm_vec3_add(eye, forward, target);
    vec3 world_up = {0.0f, 1.0f, 0.0f};
    glm_lookat(eye, target, world_up, out_view);
}

/*
 * Perspective projection maps a frustum - a truncated pyramid between
 * the near and far planes, whose angular width is set by fov_y_radians
 * and whose horizontal-to-vertical ratio is aspect_ratio - onto
 * OpenGL's canonical clip cube [-1,1]^3. The defining property is the
 * divide by w that happens right after this matrix is applied (the
 * implicit clip-space -> NDC step): points further from the camera end
 * up with a larger w, so their x/y get divided down more, which is what
 * makes distant objects appear smaller - the standard perspective
 * effect that matches human vision.
 *
 * Orthographic projection maps an axis-aligned box directly onto the
 * same clip cube with no such w divide (w stays 1), so an object's size
 * on screen does not depend on its distance from the camera: parallel
 * lines stay parallel and a 1-meter cube looks the same size whether it
 * is near or far. That is what makes it the right choice for 2D/UI
 * rendering, isometric/CAD-style views, and shadow-map cascades for
 * directional lights - none of which want distance-based
 * foreshortening.
 */
void camera_get_projection_matrix(const Camera *camera, mat4 out_projection) {
    ASSERT(camera != nullptr);
    if (camera == nullptr) {
        return;
    }
    if (camera->projection_type == CAMERA_PROJECTION_PERSPECTIVE) {
        glm_perspective(camera->fov_y_radians, camera->aspect_ratio, camera->near_plane,
                         camera->far_plane, out_projection);
    } else {
        const float half_width = camera->ortho_half_height * camera->aspect_ratio;
        glm_ortho(-half_width, half_width, -camera->ortho_half_height, camera->ortho_half_height,
                  camera->near_plane, camera->far_plane, out_projection);
    }
}

void camera_get_view_projection_matrix(const Camera *camera, mat4 out_view_projection) {
    ASSERT(camera != nullptr);
    if (camera == nullptr) {
        return;
    }
    mat4 view;
    mat4 projection;
    camera_get_view_matrix(camera, view);
    camera_get_projection_matrix(camera, projection);
    glm_mat4_mul(projection, view, out_view_projection);
}

/*
 * Moves along the camera's own basis rather than world axes: offset[0]
 * along "right" and offset[2] along "forward" give the familiar
 * strafe/walk controls that stay aligned with where the camera is
 * looking. offset[1] deliberately moves along WORLD up rather than the
 * camera's local up: local up tilts with pitch (see camera_get_up), so
 * moving along it while looking up or down would drift the camera
 * forward/backward as a side effect of a purely vertical input; using
 * world up keeps "move up" doing only that, which is the behavior
 * players expect from an FPS-style camera.
 */
void camera_move_local(Camera *camera, vec3 offset) {
    ASSERT(camera != nullptr);
    if (camera == nullptr) {
        return;
    }
    vec3 forward;
    vec3 right;
    camera_get_forward(camera, forward);
    camera_get_right(camera, right);
    vec3 world_up = {0.0f, 1.0f, 0.0f};

    vec3 delta = {0.0f, 0.0f, 0.0f};
    glm_vec3_muladds(right, offset[0], delta);
    glm_vec3_muladds(world_up, offset[1], delta);
    glm_vec3_muladds(forward, offset[2], delta);

    glm_vec3_add(camera->position, delta, camera->position);
}

void camera_rotate(Camera *camera, float delta_yaw_radians, float delta_pitch_radians) {
    ASSERT(camera != nullptr);
    if (camera == nullptr) {
        return;
    }
    camera->yaw_radians += delta_yaw_radians;
    camera->pitch_radians += delta_pitch_radians;
    if (camera->pitch_radians > CAMERA_PITCH_LIMIT_RADIANS) {
        camera->pitch_radians = CAMERA_PITCH_LIMIT_RADIANS;
    }
    if (camera->pitch_radians < -CAMERA_PITCH_LIMIT_RADIANS) {
        camera->pitch_radians = -CAMERA_PITCH_LIMIT_RADIANS;
    }
}

/*
 * Gribb & Hartmann's technique (2001): each clip-space boundary plane
 * (x = -w, x = +w, y = -w, y = +w, z = -w, z = +w) becomes, after
 * substituting the row of the combined view-projection matrix M that
 * computes clip-space x/y/z/w from a world-space point, a plane
 * equation directly in world space. Concretely, for a world-space point
 * p (homogeneous, implicit w=1), clip-space x = row0(M).p and clip-space
 * w = row3(M).p; the clip-space requirement "-w <= x" becomes
 * "row3(M).p + row0(M).p >= 0", i.e. the left plane's coefficients are
 * simply row3(M) + row0(M). The other five planes follow the same
 * pattern with the matching row and sign. cglm stores mat4 column-major
 * (m[col][row]), so "row i of M" as a vec4 is
 * {m[0][i], m[1][i], m[2][i], m[3][i]}.
 */
Frustum camera_get_frustum(const Camera *camera) {
    ASSERT(camera != nullptr);
    Frustum frustum = {0};
    if (camera == nullptr) {
        return frustum;
    }

    mat4 view_projection;
    camera_get_view_projection_matrix(camera, view_projection);

    const vec4 row0 = {view_projection[0][0], view_projection[1][0], view_projection[2][0],
                        view_projection[3][0]};
    const vec4 row1 = {view_projection[0][1], view_projection[1][1], view_projection[2][1],
                        view_projection[3][1]};
    const vec4 row2 = {view_projection[0][2], view_projection[1][2], view_projection[2][2],
                        view_projection[3][2]};
    const vec4 row3 = {view_projection[0][3], view_projection[1][3], view_projection[2][3],
                        view_projection[3][3]};

    for (int i = 0; i < 4; ++i) {
        frustum.planes[0][i] = row3[i] + row0[i]; /* left */
        frustum.planes[1][i] = row3[i] - row0[i]; /* right */
        frustum.planes[2][i] = row3[i] + row1[i]; /* bottom */
        frustum.planes[3][i] = row3[i] - row1[i]; /* top */
        frustum.planes[4][i] = row3[i] + row2[i]; /* near */
        frustum.planes[5][i] = row3[i] - row2[i]; /* far */
    }

    for (int p = 0; p < 6; ++p) {
        const float length = sqrtf(frustum.planes[p][0] * frustum.planes[p][0] +
                                    frustum.planes[p][1] * frustum.planes[p][1] +
                                    frustum.planes[p][2] * frustum.planes[p][2]);
        if (length > 0.0f) {
            frustum.planes[p][0] /= length;
            frustum.planes[p][1] /= length;
            frustum.planes[p][2] /= length;
            frustum.planes[p][3] /= length;
        }
    }

    return frustum;
}

bool frustum_contains_sphere(const Frustum *frustum, vec3 center, float radius) {
    ASSERT(frustum != nullptr);
    if (frustum == nullptr) {
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        const float distance = frustum->planes[i][0] * center[0] + frustum->planes[i][1] * center[1] +
                                frustum->planes[i][2] * center[2] + frustum->planes[i][3];
        if (distance < -radius) {
            return false;
        }
    }
    return true;
}

bool frustum_contains_point(const Frustum *frustum, vec3 point) {
    return frustum_contains_sphere(frustum, point, 0.0f);
}
