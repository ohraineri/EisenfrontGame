/*
 * Camera is a pure math module (no GL, no window) - these tests run
 * with no video driver at all.
 */
#include "eisenfront/camera.h"

#include "unity.h"

#include <math.h>

#define PI 3.14159265358979323846f

void setUp(void) {
}

void tearDown(void) {
}

static void test_forward_at_zero_yaw_pitch_points_along_x(void) {
    vec3   position = {0.0f, 0.0f, 0.0f};
    Camera camera = camera_create_perspective(position, glm_rad(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);

    vec3 forward;
    camera_get_forward(&camera, forward);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, forward[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, forward[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, forward[2]);
}

static void test_forward_at_quarter_turn_yaw_points_along_z(void) {
    vec3   position = {0.0f, 0.0f, 0.0f};
    Camera camera = camera_create_perspective(position, glm_rad(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    camera_rotate(&camera, PI / 2.0f, 0.0f);

    vec3 forward;
    camera_get_forward(&camera, forward);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, forward[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, forward[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, forward[2]);
}

static void test_forward_right_up_are_orthonormal(void) {
    vec3   position = {1.0f, 2.0f, 3.0f};
    Camera camera = camera_create_perspective(position, glm_rad(75.0f), 1.5f, 0.1f, 50.0f);
    camera_rotate(&camera, 0.7f, 0.4f);

    vec3 forward, right, up;
    camera_get_forward(&camera, forward);
    camera_get_right(&camera, right);
    camera_get_up(&camera, up);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, glm_vec3_norm(forward));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, glm_vec3_norm(right));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, glm_vec3_norm(up));

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, glm_vec3_dot(forward, right));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, glm_vec3_dot(forward, up));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, glm_vec3_dot(right, up));
}

static void test_view_matrix_maps_eye_to_origin(void) {
    vec3   position = {3.0f, 4.0f, 5.0f};
    Camera camera = camera_create_perspective(position, glm_rad(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    camera_rotate(&camera, 1.1f, -0.3f);

    mat4 view;
    camera_get_view_matrix(&camera, view);

    vec3 transformed_eye;
    glm_mat4_mulv3(view, position, 1.0f, transformed_eye);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, transformed_eye[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, transformed_eye[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, transformed_eye[2]);
}

static void test_perspective_and_orthographic_projections_differ(void) {
    vec3   position = {0.0f, 0.0f, 0.0f};
    Camera perspective_camera =
        camera_create_perspective(position, glm_rad(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    Camera ortho_camera = camera_create_orthographic(position, 5.0f, 16.0f / 9.0f, 0.1f, 100.0f);

    mat4 perspective_matrix, ortho_matrix;
    camera_get_projection_matrix(&perspective_camera, perspective_matrix);
    camera_get_projection_matrix(&ortho_camera, ortho_matrix);

    /* Perspective's [3][3] (the w row's w column) is 0 - w comes from
     * -z, not a constant - while orthographic's is 1 - w is always 1,
     * which is exactly the "no distance-based foreshortening" property
     * the file header comment explains. */
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, perspective_matrix[3][3]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, ortho_matrix[3][3]);
}

static void test_move_local_forward_moves_along_forward_vector(void) {
    vec3   position = {0.0f, 0.0f, 0.0f};
    Camera camera = camera_create_perspective(position, glm_rad(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    camera_rotate(&camera, 0.6f, 0.0f); /* yaw only, so forward stays in the XZ plane */

    vec3 forward;
    camera_get_forward(&camera, forward);

    vec3 offset = {0.0f, 0.0f, 5.0f};
    camera_move_local(&camera, offset);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, forward[0] * 5.0f, camera.position[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, forward[1] * 5.0f, camera.position[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, forward[2] * 5.0f, camera.position[2]);
}

static void test_move_local_up_uses_world_up_not_local_up(void) {
    vec3   position = {0.0f, 0.0f, 0.0f};
    Camera camera = camera_create_perspective(position, glm_rad(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    camera_rotate(&camera, 0.0f, 0.5f); /* pitched up: local up now tilts away from world up */

    vec3 offset = {0.0f, 2.0f, 0.0f};
    camera_move_local(&camera, offset);

    /* Moving "up" while pitched must be pure world-Y motion - see the
     * camera_move_local file comment on why local up is not used here. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, camera.position[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, camera.position[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, camera.position[2]);
}

static void test_rotate_clamps_pitch(void) {
    vec3   position = {0.0f, 0.0f, 0.0f};
    Camera camera = camera_create_perspective(position, glm_rad(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);

    camera_rotate(&camera, 0.0f, PI); /* way past +90 degrees */
    TEST_ASSERT_TRUE(camera.pitch_radians < PI / 2.0f);
    TEST_ASSERT_TRUE(camera.pitch_radians > 1.4f);

    camera_rotate(&camera, 0.0f, -2.0f * PI); /* way past -90 degrees */
    TEST_ASSERT_TRUE(camera.pitch_radians > -PI / 2.0f);
    TEST_ASSERT_TRUE(camera.pitch_radians < -1.4f);
}

static void test_frustum_contains_sphere_directly_ahead(void) {
    vec3   position = {-5.0f, 0.0f, 0.0f};
    Camera camera = camera_create_perspective(position, glm_rad(90.0f), 1.0f, 0.1f, 100.0f);
    /* yaw = 0: looks down +X, straight toward the origin. */

    Frustum frustum = camera_get_frustum(&camera);

    vec3 origin = {0.0f, 0.0f, 0.0f};
    TEST_ASSERT_TRUE(frustum_contains_sphere(&frustum, origin, 1.0f));
}

static void test_frustum_excludes_point_behind_camera(void) {
    vec3   position = {0.0f, 0.0f, 0.0f};
    Camera camera = camera_create_perspective(position, glm_rad(90.0f), 1.0f, 0.1f, 100.0f);
    /* yaw = 0: looks down +X; a point behind is at -X. */

    Frustum frustum = camera_get_frustum(&camera);

    vec3 behind = {-10.0f, 0.0f, 0.0f};
    TEST_ASSERT_FALSE(frustum_contains_point(&frustum, behind));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_forward_at_zero_yaw_pitch_points_along_x);
    RUN_TEST(test_forward_at_quarter_turn_yaw_points_along_z);
    RUN_TEST(test_forward_right_up_are_orthonormal);
    RUN_TEST(test_view_matrix_maps_eye_to_origin);
    RUN_TEST(test_perspective_and_orthographic_projections_differ);
    RUN_TEST(test_move_local_forward_moves_along_forward_vector);
    RUN_TEST(test_move_local_up_uses_world_up_not_local_up);
    RUN_TEST(test_rotate_clamps_pitch);
    RUN_TEST(test_frustum_contains_sphere_directly_ahead);
    RUN_TEST(test_frustum_excludes_point_behind_camera);

    return UNITY_END();
}
