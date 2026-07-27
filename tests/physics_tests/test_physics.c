/*
 * Physics is a pure math/logic module (no GL, no window) - these tests
 * run with no video driver at all.
 */
#include "eisenfront/physics.h"

#include "unity.h"

#include <math.h>

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
        character_controller_move(controller, step, 1.0f / 60.0f, position);
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
    character_controller_move(controller, no_move, 1.0f / 60.0f, position);
    TEST_ASSERT_TRUE(character_controller_is_grounded(controller));

    vec3 ground_normal;
    character_controller_get_ground_normal(controller, ground_normal);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ground_normal[0]);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, ground_normal[1]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, ground_normal[2]);

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_grounded_hysteresis_survives_a_brief_gap(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    RigidBodyDesc floor_desc = rigid_body_desc_default();
    floor_desc.type = BODY_TYPE_STATIC;
    floor_desc.shape = shape_box((vec3){10.0f, 0.5f, 10.0f});
    BodyId floor = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &floor_desc, &floor));

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
    character_controller_move(controller, no_move, 1.0f / 60.0f, position);
    TEST_ASSERT_TRUE(character_controller_is_grounded(controller));

    /* Lift well past the probe's short range in a single tick
     * (simulating a small step/hop) - grounded must still read true
     * because this one miss is well inside the hysteresis window. */
    vec3 lift = {0.0f, 1.0f, 0.0f};
    character_controller_move(controller, lift, 1.0f / 60.0f, position);
    TEST_ASSERT_TRUE(character_controller_is_grounded(controller));

    /* Keep missing well past the hysteresis window (0.15s) - must
     * eventually report ungrounded. */
    for (int i = 0; i < 30; ++i) {
        character_controller_move(controller, no_move, 1.0f / 60.0f, position);
    }
    TEST_ASSERT_FALSE(character_controller_is_grounded(controller));

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_ground_normal_defaults_upright_before_contact(void) {
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

    vec3 normal;
    character_controller_get_ground_normal(controller, normal);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, normal[0]);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, normal[1]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, normal[2]);

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_ground_normal_from_sphere_is_actually_computed(void) {
    /* Sphere radius 1.0 centered at the origin; the vertical probe line
     * at x=0 crosses the sphere surface at y = sqrt(1.0^2 - 0.3^2) =
     * sqrt(0.91) ~= 0.9539 when the sphere is offset 0.3 in x. Placing
     * the capsule so its probe just grazes that point (within the
     * radius*0.3=0.12 probe tolerance) proves the reported normal is a
     * real computation (hit_point - center, normalized) rather than
     * the (0,1,0) default every box floor reports - a purely-vertical
     * result here would mean the offset was silently ignored. */
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    RigidBodyDesc sphere_desc = rigid_body_desc_default();
    sphere_desc.type = BODY_TYPE_STATIC;
    sphere_desc.shape = shape_sphere(1.0f);
    sphere_desc.position[0] = 0.3f;
    BodyId sphere = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &sphere_desc, &sphere));

    const CharacterControllerDesc controller_desc = {
        .radius = 0.4f,
        .half_height = 0.9f,
        /* probe_origin.y = capsule.y - half_height - radius = capsule.y - 1.3;
         * want probe_origin.y ~= 0.9539 + 0.05 (just within the 0.12 tolerance). */
        .position = {0.0f, 1.3f + 0.9539f + 0.05f, 0.0f},
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    vec3 position;
    vec3 no_move = {0.0f, 0.0f, 0.0f};
    character_controller_move(controller, no_move, 1.0f / 60.0f, position);
    TEST_ASSERT_TRUE(character_controller_is_grounded(controller));

    vec3 normal;
    character_controller_get_ground_normal(controller, normal);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, -0.3f, normal[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.9539f, normal[1]);

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_resize_shrinking_always_succeeds(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    const CharacterControllerDesc controller_desc = {
        .radius = 0.4f,
        .half_height = 0.9f,
        .position = {0.0f, 1.3f, 0.0f}, /* bottom at y = 1.3 - 0.9 - 0.4 = 0.0 */
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    TEST_ASSERT_TRUE(character_controller_resize(controller, 0.4f, 0.5f));

    /* Bottom point must stay fixed: new center = 0.0 + 0.5 + 0.4 = 0.9. */
    vec3 position;
    character_controller_get_position(controller, position);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.9f, position[1]);

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_resize_growing_succeeds_in_open_space(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    const CharacterControllerDesc controller_desc = {
        .radius = 0.4f,
        .half_height = 0.5f,
        .position = {0.0f, 0.9f, 0.0f}, /* crouched, bottom at y = 0.0 */
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    TEST_ASSERT_TRUE(character_controller_resize(controller, 0.4f, 0.9f));

    /* Bottom point must stay fixed: new center = 0.0 + 0.9 + 0.4 = 1.3. */
    vec3 position;
    character_controller_get_position(controller, position);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.3f, position[1]);

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_resize_growing_blocked_by_low_ceiling(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    /* Ceiling spans y in [1.2, 2.0]. Crouched (radius 0.3, half_height
     * 0.2, bottom at y=0.0) tops out at y=1.0 - clear of the ceiling.
     * Standing back up (radius 0.3, half_height 0.5) from the same
     * bottom would top out at y=1.6, which pokes into the ceiling. */
    RigidBodyDesc ceiling_desc = rigid_body_desc_default();
    ceiling_desc.type = BODY_TYPE_STATIC;
    ceiling_desc.shape = shape_box((vec3){5.0f, 0.4f, 5.0f});
    ceiling_desc.position[1] = 1.6f;
    BodyId ceiling = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &ceiling_desc, &ceiling));

    const CharacterControllerDesc controller_desc = {
        .radius = 0.3f,
        .half_height = 0.2f,
        .position = {0.0f, 0.5f, 0.0f}, /* crouched, bottom at y = 0.0 */
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    TEST_ASSERT_FALSE(character_controller_resize(controller, 0.3f, 0.5f));

    /* Must remain exactly where and how it was - no partial resize. */
    vec3 position;
    character_controller_get_position(controller, position);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, position[1]);

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_zero_delta_move_is_a_no_op(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    const CharacterControllerDesc controller_desc = {
        .radius = 0.4f,
        .half_height = 0.9f,
        .position = {3.0f, 7.0f, -2.0f},
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    /* Open space, no bodies at all - a zero displacement must leave the
     * position exactly as it was, not introduce any drift or spurious
     * "correction" from a call that should be a pure no-op. */
    vec3 zero = {0.0f, 0.0f, 0.0f};
    vec3 position;
    character_controller_move(controller, zero, 1.0f / 60.0f, position);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, position[0]);
    TEST_ASSERT_EQUAL_FLOAT(7.0f, position[1]);
    TEST_ASSERT_EQUAL_FLOAT(-2.0f, position[2]);

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_wall_slide_preserves_tangential_no_energy_gain(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    RigidBodyDesc wall_desc = rigid_body_desc_default();
    wall_desc.type = BODY_TYPE_STATIC;
    /* z half-extent generous enough that 60 ticks of 0.3 units/tick of
     * sliding (18 units total) never runs off the end of the wall -
     * this test wants an effectively-infinite wall, not an outside
     * corner (that's test_character_controller_outside_corner_slides_around_smoothly's job). */
    wall_desc.shape = shape_box((vec3){0.5f, 5.0f, 50.0f});
    wall_desc.position[0] = 2.0f; /* near face at x=1.5 */
    BodyId wall = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &wall_desc, &wall));

    const CharacterControllerDesc controller_desc = {
        .radius = 0.4f,
        .half_height = 0.9f,
        .position = {0.0f, 0.0f, 0.0f},
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    /* Moving diagonally into the wall (+x, +z): the wall only opposes
     * x, so z must keep accumulating at essentially the full requested
     * rate (tangential preservation), while x gets capped at the wall.
     * The resulting per-tick displacement magnitude must never exceed
     * the requested one - sliding must not inject energy. */
    vec3       position = {0.0f, 0.0f, 0.0f};
    const float dt = 1.0f / 60.0f;
    float       total_z = 0.0f;
    for (int i = 0; i < 60; ++i) {
        vec3 previous;
        glm_vec3_copy(position, previous);
        vec3 step = {0.3f * dt * 60.0f, 0.0f, 0.3f * dt * 60.0f}; /* 0.3 units/tick along each axis */
        character_controller_move(controller, step, dt, position);

        vec3 actual_delta;
        glm_vec3_sub(position, previous, actual_delta);
        TEST_ASSERT_TRUE(glm_vec3_norm(actual_delta) <= glm_vec3_norm(step) + 0.001f);
        total_z += (position[2] - previous[2]);
    }
    TEST_ASSERT_TRUE(position[0] < 1.15f);   /* stopped at the wall */
    TEST_ASSERT_TRUE(total_z > 0.9f * 60.0f * 0.3f * dt); /* z kept accumulating, not eaten by the wall */

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_inside_corner_stops_without_penetration(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    /* Two walls forming a concave (inside) corner: one blocking +x
     * (near face x=1.6), one blocking +z (near face z=1.6). */
    RigidBodyDesc wall_x = rigid_body_desc_default();
    wall_x.type = BODY_TYPE_STATIC;
    wall_x.shape = shape_box((vec3){0.5f, 5.0f, 5.0f});
    wall_x.position[0] = 2.1f;
    BodyId wall_x_id = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &wall_x, &wall_x_id));

    RigidBodyDesc wall_z = rigid_body_desc_default();
    wall_z.type = BODY_TYPE_STATIC;
    wall_z.shape = shape_box((vec3){5.0f, 5.0f, 0.5f});
    wall_z.position[2] = 2.1f;
    BodyId wall_z_id = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &wall_z, &wall_z_id));

    const CharacterControllerDesc controller_desc = {
        .radius = 0.4f,
        .half_height = 0.9f,
        .position = {0.0f, 0.0f, 0.0f},
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    /* Drive straight into the corner formed by both walls at once. */
    vec3 position;
    for (int i = 0; i < 40; ++i) {
        vec3 step = {0.3f, 0.0f, 0.3f};
        character_controller_move(controller, step, 1.0f / 60.0f, position);
    }

    /* Stopped by both walls (near faces at 1.6 - capsule radius 0.4 =
     * 1.2 is the deepest either axis should reach), never tunneled
     * through either one. */
    TEST_ASSERT_TRUE(position[0] < 1.25f);
    TEST_ASSERT_TRUE(position[2] < 1.25f);

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_outside_corner_slides_around_smoothly(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    /* A single wall segment, not spanning the full z range (unlike the
     * infinite-looking walls above) - it ends at z=0, forming a convex
     * (outside) corner there. Near face at x=1.5. */
    RigidBodyDesc wall_desc = rigid_body_desc_default();
    wall_desc.type = BODY_TYPE_STATIC;
    wall_desc.shape = shape_box((vec3){0.5f, 5.0f, 2.5f});
    wall_desc.position[0] = 2.0f;
    wall_desc.position[2] = -2.5f; /* spans z in [-5, 0] */
    BodyId wall = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &wall_desc, &wall));

    const CharacterControllerDesc controller_desc = {
        .radius = 0.4f,
        .half_height = 0.9f,
        .position = {0.0f, 0.0f, -3.0f}, /* beside the wall */
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    /* Press toward the wall (+x) while sliding along it (+z) past its
     * end (z=0) - once clear of the wall's extent, +x should open up
     * (round the outside corner) without snagging on the edge. */
    vec3 position = {0.0f, 0.0f, -3.0f};
    for (int i = 0; i < 90; ++i) {
        vec3 step = {0.3f, 0.0f, 0.15f};
        character_controller_move(controller, step, 1.0f / 60.0f, position);
    }

    /* Past the wall's z-extent and well into +x territory that the
     * wall would have blocked while still beside it. */
    TEST_ASSERT_TRUE(position[2] > 0.5f);
    TEST_ASSERT_TRUE(position[0] > 2.5f);

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_competing_normals_settle_without_oscillation(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    /* Two walls facing each other with a gap (0.4m) narrower than the
     * capsule's diameter (0.8m) - spawning centered in the gap
     * necessarily overlaps both at once, with opposing normals. */
    RigidBodyDesc wall_a = rigid_body_desc_default();
    wall_a.type = BODY_TYPE_STATIC;
    wall_a.shape = shape_box((vec3){0.5f, 5.0f, 5.0f});
    wall_a.position[0] = 0.7f;
    BodyId wall_a_id = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &wall_a, &wall_a_id));

    RigidBodyDesc wall_b = rigid_body_desc_default();
    wall_b.type = BODY_TYPE_STATIC;
    wall_b.shape = shape_box((vec3){0.5f, 5.0f, 5.0f});
    wall_b.position[0] = -0.7f;
    BodyId wall_b_id = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &wall_b, &wall_b_id));

    const CharacterControllerDesc controller_desc = {
        .radius = 0.4f,
        .half_height = 0.9f,
        .position = {0.0f, 0.0f, 0.0f},
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    vec3 zero = {0.0f, 0.0f, 0.0f};
    vec3 position;
    character_controller_move(controller, zero, 1.0f / 60.0f, position);
    const float settled_x = position[0];
    TEST_ASSERT_TRUE(isfinite(settled_x));

    /* Continuing to call move() (as a real per-tick loop would) must
     * reach a fixed point, not oscillate indefinitely between the two
     * competing normals. */
    for (int i = 0; i < 10; ++i) {
        character_controller_move(controller, zero, 1.0f / 60.0f, position);
        TEST_ASSERT_FLOAT_WITHIN(0.01f, settled_x, position[0]);
    }

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_repeated_frames_against_wall_remain_stable(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    RigidBodyDesc wall_desc = rigid_body_desc_default();
    wall_desc.type = BODY_TYPE_STATIC;
    wall_desc.shape = shape_box((vec3){0.5f, 5.0f, 5.0f});
    wall_desc.position[0] = 2.0f;
    BodyId wall = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &wall_desc, &wall));

    const CharacterControllerDesc controller_desc = {
        .radius = 0.4f,
        .half_height = 0.9f,
        .position = {0.0f, 0.0f, 0.0f},
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    /* Hold a direction key into the wall for a long stretch (200
     * ticks) - resting distance must stabilize, not creep deeper tick
     * over tick from repeated floating-point correction. */
    vec3  position = {0.0f, 0.0f, 0.0f};
    float previous_x = position[0];
    for (int i = 0; i < 200; ++i) {
        vec3 step = {0.2f, 0.0f, 0.0f};
        character_controller_move(controller, step, 1.0f / 60.0f, position);
        if (i > 20) {
            /* Past initial approach, resting position should have
             * settled - no continued net drift toward/through the wall. */
            TEST_ASSERT_FLOAT_WITHIN(0.02f, previous_x, position[0]);
        }
        previous_x = position[0];
    }
    TEST_ASSERT_TRUE(position[0] < 1.15f);

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_high_speed_legitimate_movement_no_tunneling(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    RigidBodyDesc wall_desc = rigid_body_desc_default();
    wall_desc.type = BODY_TYPE_STATIC;
    wall_desc.shape = shape_box((vec3){0.5f, 5.0f, 5.0f});
    wall_desc.position[0] = 4.0f;
    BodyId wall = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &wall_desc, &wall));

    const CharacterControllerDesc controller_desc = {
        .radius = 0.4f,
        .half_height = 0.9f,
        .position = {0.0f, 0.0f, 0.0f},
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    /* Realistic sprint-speed displacement per tick (well above the
     * Infantry Controller's ~4.4 m/s sprint at 60Hz: ~0.35m/tick used
     * here, comfortably faster) against a normal (non-thin, 1m thick)
     * wall - must never tunnel. */
    vec3 position;
    for (int i = 0; i < 40; ++i) {
        vec3 step = {0.35f, 0.0f, 0.0f};
        character_controller_move(controller, step, 1.0f / 60.0f, position);
        TEST_ASSERT_TRUE(position[0] < 3.55f); /* never past the wall's near face */
    }
    TEST_ASSERT_TRUE(position[0] < 3.55f && position[0] > 3.0f); /* actually stopped at it, not short */

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_thin_obstacle_blocked_by_substep_mitigation(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    /* A thin wall (0.1m thick) - thinner than the capsule's own
     * diameter (0.8m). A single naive discrete test of a large
     * displacement's END point alone could land clean past it; the
     * substep mitigation in character_controller_move() must still
     * catch it for a realistic (if fast) single-tick displacement. */
    RigidBodyDesc wall_desc = rigid_body_desc_default();
    wall_desc.type = BODY_TYPE_STATIC;
    wall_desc.shape = shape_box((vec3){0.05f, 5.0f, 5.0f});
    wall_desc.position[0] = 2.0f;
    BodyId wall = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &wall_desc, &wall));

    const CharacterControllerDesc controller_desc = {
        .radius = 0.4f,
        .half_height = 0.9f,
        .position = {0.0f, 0.0f, 0.0f},
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    /* One single tick, displacement large enough (1.5 units) that its
     * END point (x=1.5) would land exactly at - not past - the wall;
     * try a bit more to confirm it's actually caught mid-flight, not
     * just coincidentally short. */
    vec3 step = {2.0f, 0.0f, 0.0f};
    vec3 position;
    character_controller_move(controller, step, 1.0f / 60.0f, position);

    /* Must be stopped at the wall's near face (2.0 - 0.05 - 0.4 =
     * 1.55), not past it at x=2.0 (where the undivided end point
     * would have landed, clean through the thin wall). */
    TEST_ASSERT_TRUE(position[0] < 1.65f);

    character_controller_destroy(controller);
    physics_world_destroy(world);
}

static void test_character_controller_thin_obstacle_extreme_speed_known_limitation(void) {
    /* Documents a known, deliberate residual limitation (see
     * character_controller_move()'s file comment): the substep
     * mitigation bounds each test step to at most
     * CHARACTER_CONTROLLER_MAX_SUBSTEPS (8) substeps regardless of how
     * large the total displacement is. A displacement extreme enough
     * that even the smallest achievable substep still exceeds the
     * obstacle's thickness can still tunnel. This is not a regression
     * to "fix" quietly - true continuous collision detection (a swept
     * capsule test) is required to close it, and is tracked as
     * separate future engine work, not part of this milestone. If this
     * assertion ever starts failing because tunneling no longer
     * happens, that's good news - update this test to match, don't
     * treat the failure as a bug. */
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));

    RigidBodyDesc wall_desc = rigid_body_desc_default();
    wall_desc.type = BODY_TYPE_STATIC;
    wall_desc.shape = shape_box((vec3){0.05f, 5.0f, 5.0f});
    wall_desc.position[0] = 2.0f;
    BodyId wall = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &wall_desc, &wall));

    const CharacterControllerDesc controller_desc = {
        .radius = 0.4f,
        .half_height = 0.9f,
        .position = {0.0f, 0.0f, 0.0f},
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, character_controller_create(world, &controller_desc, &controller));

    /* 200 units in one tick (absurd - far beyond any real gameplay
     * speed) / 8 max substeps = 25 units per substep, vastly larger
     * than the 0.1-unit-thick wall - each substep's END point alone
     * lands past the wall with room to spare. */
    vec3 step = {200.0f, 0.0f, 0.0f};
    vec3 position;
    character_controller_move(controller, step, 1.0f / 60.0f, position);
    TEST_ASSERT_TRUE(position[0] > 2.0f); /* tunneled clean through */

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
    RUN_TEST(test_character_controller_grounded_hysteresis_survives_a_brief_gap);
    RUN_TEST(test_character_controller_ground_normal_defaults_upright_before_contact);
    RUN_TEST(test_character_controller_ground_normal_from_sphere_is_actually_computed);
    RUN_TEST(test_character_controller_resize_shrinking_always_succeeds);
    RUN_TEST(test_character_controller_resize_growing_succeeds_in_open_space);
    RUN_TEST(test_character_controller_resize_growing_blocked_by_low_ceiling);
    RUN_TEST(test_character_controller_zero_delta_move_is_a_no_op);
    RUN_TEST(test_character_controller_wall_slide_preserves_tangential_no_energy_gain);
    RUN_TEST(test_character_controller_inside_corner_stops_without_penetration);
    RUN_TEST(test_character_controller_outside_corner_slides_around_smoothly);
    RUN_TEST(test_character_controller_competing_normals_settle_without_oscillation);
    RUN_TEST(test_character_controller_repeated_frames_against_wall_remain_stable);
    RUN_TEST(test_character_controller_high_speed_legitimate_movement_no_tunneling);
    RUN_TEST(test_character_controller_thin_obstacle_blocked_by_substep_mitigation);
    RUN_TEST(test_character_controller_thin_obstacle_extreme_speed_known_limitation);
    RUN_TEST(test_character_controller_set_position_teleports);

    return UNITY_END();
}
