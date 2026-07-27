/*
 * Physics is a pure math/logic module (no GL, no window) - these tests
 * run with no video driver at all.
 */
#include "eisenfront/physics.h"

#include "unity.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_create_destroy_body(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    RigidBodyDesc desc = rigid_body_desc_default();
    BodyId        body = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &desc, &body));
    TEST_ASSERT_TRUE(rigid_body_is_valid(world, body));

    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_destroy(world, body));
    TEST_ASSERT_FALSE(rigid_body_is_valid(world, body));

    physics_world_destroy(world);
}

static void test_stale_id_after_slot_reuse(void) {
    PhysicsWorldDesc world_desc = physics_world_desc_default();
    world_desc.max_bodies = 1;
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(&world_desc, &world));

    RigidBodyDesc desc = rigid_body_desc_default();
    BodyId        first = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &desc, &first));
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_destroy(world, first));

    BodyId second = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &desc, &second));
    TEST_ASSERT_NOT_EQUAL(first, second);
    TEST_ASSERT_FALSE(rigid_body_is_valid(world, first));

    physics_world_destroy(world);
}

static void test_body_capacity_exceeded(void) {
    PhysicsWorldDesc world_desc = physics_world_desc_default();
    world_desc.max_bodies = 1;
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(&world_desc, &world));

    RigidBodyDesc desc = rigid_body_desc_default();
    BodyId        a, b;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &desc, &a));
    TEST_ASSERT_EQUAL(RESULT_ERROR_CAPACITY_EXCEEDED, rigid_body_create(world, &desc, &b));

    physics_world_destroy(world);
}

static void test_gravity_and_transform_queries(void) {
    PhysicsWorldDesc world_desc = physics_world_desc_default();
    world_desc.gravity[0] = 0.0f;
    world_desc.gravity[1] = -10.0f;
    world_desc.gravity[2] = 0.0f;
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(&world_desc, &world));

    RigidBodyDesc desc = rigid_body_desc_default();
    desc.type = BODY_TYPE_DYNAMIC;
    desc.linear_damping = 0.0f;
    desc.position[0] = 0.0f;
    desc.position[1] = 100.0f;
    desc.position[2] = 0.0f;
    BodyId body = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &desc, &body));

    physics_world_step(world, 0.1f);

    vec3 position;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_get_position(world, body, position));
    /* velocity after one 0.1s step = -1.0, position integrated by that
     * velocity over the same step = 100.0 - 0.1 = 99.9 */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 99.9f, position[1]);

    vec3 velocity;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_get_linear_velocity(world, body, velocity));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -1.0f, velocity[1]);

    physics_world_destroy(world);
}

static void test_set_transform_teleports(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    RigidBodyDesc desc = rigid_body_desc_default();
    BodyId        body = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &desc, &body));

    vec3   new_position = {5.0f, 6.0f, 7.0f};
    versor identity;
    glm_quat_identity(identity);
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_set_transform(world, body, new_position, identity));

    vec3 position;
    rigid_body_get_position(world, body, position);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 5.0f, position[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 6.0f, position[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 7.0f, position[2]);

    physics_world_destroy(world);
}

static void test_dynamic_sphere_rests_on_static_floor(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    RigidBodyDesc floor_desc = rigid_body_desc_default();
    floor_desc.type = BODY_TYPE_STATIC;
    floor_desc.shape = shape_box((vec3){10.0f, 0.5f, 10.0f});
    floor_desc.position[1] = 0.0f;
    BodyId floor = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &floor_desc, &floor));

    RigidBodyDesc ball_desc = rigid_body_desc_default();
    ball_desc.type = BODY_TYPE_DYNAMIC;
    ball_desc.shape = shape_sphere(0.5f);
    ball_desc.restitution = 0.0f;
    ball_desc.position[1] = 3.0f;
    BodyId ball = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &ball_desc, &ball));

    for (int i = 0; i < 300; ++i) {
        physics_world_step(world, 1.0f / 60.0f);
    }

    vec3 position;
    rigid_body_get_position(world, ball, position);
    /* Floor top is at y=0.5, sphere radius 0.5, so it should settle with
     * its center near y=1.0. */
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 1.0f, position[1]);

    physics_world_destroy(world);
}

static void test_collision_layers_prevent_resolution(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    RigidBodyDesc a_desc = rigid_body_desc_default();
    a_desc.type = BODY_TYPE_DYNAMIC;
    a_desc.layer = 1u;
    a_desc.layer_mask = 1u; /* only collides with layer 1 */
    a_desc.position[0] = 0.0f;
    BodyId a = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &a_desc, &a));

    RigidBodyDesc b_desc = rigid_body_desc_default();
    b_desc.type = BODY_TYPE_DYNAMIC;
    b_desc.layer = 2u;
    b_desc.layer_mask = 2u; /* only collides with layer 2 - mismatched with a */
    b_desc.position[0] = 0.1f;
    BodyId b = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &b_desc, &b));

    for (int i = 0; i < 10; ++i) {
        physics_world_step(world, 1.0f / 60.0f);
    }

    /* Deeply overlapping (0.1 apart, both radius 0.5) but on
     * non-interacting layers: neither should have been pushed apart. */
    vec3 pos_a, pos_b;
    rigid_body_get_position(world, a, pos_a);
    rigid_body_get_position(world, b, pos_b);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, pos_a[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.1f, pos_b[0]);

    physics_world_destroy(world);
}

static int  g_trigger_begin_count;
static int  g_trigger_end_count;
static BodyId g_last_trigger, g_last_other;

static void on_trigger_event(BodyId trigger, BodyId other, bool began, void *userdata) {
    (void)userdata;
    if (began) {
        g_trigger_begin_count += 1;
    } else {
        g_trigger_end_count += 1;
    }
    g_last_trigger = trigger;
    g_last_other = other;
}

static void test_trigger_begin_and_end_events(void) {
    g_trigger_begin_count = 0;
    g_trigger_end_count = 0;

    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    physics_world_set_trigger_callback(world, on_trigger_event, nullptr);

    RigidBodyDesc trigger_desc = rigid_body_desc_default();
    trigger_desc.type = BODY_TYPE_STATIC;
    trigger_desc.is_trigger = true;
    trigger_desc.shape = shape_sphere(1.0f);
    trigger_desc.position[0] = 0.0f;
    BodyId trigger_body = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &trigger_desc, &trigger_body));

    RigidBodyDesc passer_desc = rigid_body_desc_default();
    passer_desc.type = BODY_TYPE_KINEMATIC;
    passer_desc.shape = shape_sphere(0.1f);
    passer_desc.position[0] = -5.0f;
    BodyId passer = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &passer_desc, &passer));
    rigid_body_set_linear_velocity(world, passer, (vec3){2.0f, 0.0f, 0.0f});

    /* Enough steps to cross from x=-5 through the trigger sphere and out
     * the other side at 2 units/sec. */
    for (int i = 0; i < 300; ++i) {
        physics_world_step(world, 1.0f / 60.0f);
    }

    TEST_ASSERT_EQUAL(1, g_trigger_begin_count);
    TEST_ASSERT_EQUAL(1, g_trigger_end_count);
    TEST_ASSERT_EQUAL(trigger_body, g_last_trigger);
    TEST_ASSERT_EQUAL(passer, g_last_other);

    physics_world_destroy(world);
}

static void test_raycast_hits_closest_sphere(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    RigidBodyDesc near_desc = rigid_body_desc_default();
    near_desc.type = BODY_TYPE_STATIC;
    near_desc.shape = shape_sphere(1.0f);
    near_desc.position[0] = 5.0f;
    BodyId near_body = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &near_desc, &near_body));

    RigidBodyDesc far_desc = rigid_body_desc_default();
    far_desc.type = BODY_TYPE_STATIC;
    far_desc.shape = shape_sphere(1.0f);
    far_desc.position[0] = 10.0f;
    BodyId far_body = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &far_desc, &far_body));

    vec3       origin = {0.0f, 0.0f, 0.0f};
    vec3       direction = {1.0f, 0.0f, 0.0f};
    RaycastHit hit;
    TEST_ASSERT_TRUE(physics_raycast(world, origin, direction, 100.0f, COLLISION_LAYER_ALL, &hit));
    TEST_ASSERT_EQUAL(near_body, hit.body);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, hit.distance);

    physics_world_destroy(world);
}

static void test_raycast_misses(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    RigidBodyDesc desc = rigid_body_desc_default();
    desc.type = BODY_TYPE_STATIC;
    desc.shape = shape_sphere(1.0f);
    desc.position[1] = 20.0f; /* well off the ray's path */
    BodyId body = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &desc, &body));

    vec3       origin = {0.0f, 0.0f, 0.0f};
    vec3       direction = {1.0f, 0.0f, 0.0f};
    RaycastHit hit;
    TEST_ASSERT_FALSE(physics_raycast(world, origin, direction, 100.0f, COLLISION_LAYER_ALL, &hit));

    physics_world_destroy(world);
}

static void test_apply_impulse_changes_velocity(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    RigidBodyDesc desc = rigid_body_desc_default();
    desc.mass = 2.0f;
    BodyId body = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &desc, &body));

    vec3 impulse = {4.0f, 0.0f, 0.0f};
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_apply_impulse(world, body, impulse));

    vec3 velocity;
    rigid_body_get_linear_velocity(world, body, velocity);
    /* impulse / mass = 4 / 2 = 2 */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 2.0f, velocity[0]);

    physics_world_destroy(world);
}

static void test_character_controller_slides_along_wall(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    RigidBodyDesc wall_desc = rigid_body_desc_default();
    wall_desc.type = BODY_TYPE_STATIC;
    wall_desc.shape = shape_box((vec3){0.5f, 5.0f, 5.0f});
    wall_desc.position[0] = 2.0f;
    BodyId wall = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &wall_desc, &wall));

    CharacterControllerDesc controller_desc = {
        .radius = 0.4f,
        .half_height = 0.9f,
        .position = {0.0f, 0.0f, 0.0f},
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    /* Walk straight toward the wall; it must not penetrate past the
     * wall's near face (x = 2.0 - 0.5 - capsule_radius = 1.1). */
    vec3 position;
    for (int i = 0; i < 20; ++i) {
        vec3 step = {0.2f, 0.0f, 0.0f};
        character_controller_move(controller, step, position);
    }
    TEST_ASSERT_TRUE(position[0] < 1.15f);

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_grounded_on_floor(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    RigidBodyDesc floor_desc = rigid_body_desc_default();
    floor_desc.type = BODY_TYPE_STATIC;
    floor_desc.shape = shape_box((vec3){10.0f, 0.5f, 10.0f});
    floor_desc.position[1] = 0.0f;
    BodyId floor = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &floor_desc, &floor));

    /* Standing with feet just above the floor's top surface (y=0.5): the
     * capsule's lowest point is position.y - half_height - radius, so
     * position.y = floor_top + half_height + radius (minus a hair, to
     * land just short of the floor rather than exactly touching it). */
    CharacterControllerDesc controller_desc = {
        .radius = 0.4f,
        .half_height = 0.9f,
        .position = {0.0f, 1.79f, 0.0f},
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    vec3 position;
    vec3 no_move = {0.0f, 0.0f, 0.0f};
    character_controller_move(controller, no_move, position);
    TEST_ASSERT_TRUE(character_controller_is_grounded(controller));

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_set_position_teleports(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    const CharacterControllerDesc controller_desc = {
        .radius = 0.4f,
        .half_height = 0.9f,
        .position = {0.0f, 5.0f, 0.0f},
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    /* No collision body anywhere near the destination - a real sweep
     * via character_controller_move() would arrive there fine too, so
     * this specifically proves set_position() teleports directly
     * rather than routing through collide-and-slide. */
    vec3 destination = {40.0f, 12.0f, -40.0f};
    character_controller_set_position(controller, destination);

    vec3 position;
    character_controller_get_position(controller, position);
    TEST_ASSERT_EQUAL_FLOAT(destination[0], position[0]);
    TEST_ASSERT_EQUAL_FLOAT(destination[1], position[1]);
    TEST_ASSERT_EQUAL_FLOAT(destination[2], position[2]);

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_create_destroy_body);
    RUN_TEST(test_stale_id_after_slot_reuse);
    RUN_TEST(test_body_capacity_exceeded);
    RUN_TEST(test_gravity_and_transform_queries);
    RUN_TEST(test_set_transform_teleports);
    RUN_TEST(test_dynamic_sphere_rests_on_static_floor);
    RUN_TEST(test_collision_layers_prevent_resolution);
    RUN_TEST(test_trigger_begin_and_end_events);
    RUN_TEST(test_raycast_hits_closest_sphere);
    RUN_TEST(test_raycast_misses);
    RUN_TEST(test_apply_impulse_changes_velocity);
    RUN_TEST(test_character_controller_slides_along_wall);
    RUN_TEST(test_character_controller_grounded_on_floor);
    RUN_TEST(test_character_controller_set_position_teleports);

    return UNITY_END();
}
