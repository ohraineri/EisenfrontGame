/*
 * Integration-level tests for the crouch/jump interaction (Infantry
 * Controller spec Part VIII + the crouch/jump interaction requirements
 * given for milestone M7). These exercise infantry_entity.c against a
 * REAL PhysicsWorld with real floor/ceiling geometry - the scenarios
 * requested (a blocked stand-up, a real landing) can't be verified
 * with synthetic vectors the way player_movement_tests' pure-function
 * cases can; they need actual collision.
 */
#include "infantry_entity.h"
#include "player_tuning.h"

#include "camera_motion.h"
#include "eisenfront/physics.h"

#include "unity.h"

#define FIXED_DT (1.0f / 60.0f)
/* A jump impulse and that same tick's gravity integration both happen
 * inside one infantry_entity_update_fixed() call - the value observed
 * right after firing is the raw takeoff speed already minus one
 * tick's worth of gravity, not the raw constant. */
#define JUMP_VELOCITY_AFTER_ONE_TICK \
    (PLAYER_JUMP_SPEED_METERS_PER_SEC + PLAYER_GRAVITY_UNITS_PER_SEC2 * FIXED_DT)

/* Floor top face at y=0.5; capsule spawns resting on it (bottom at
 * 0.5) so it's grounded from tick zero. */
static void create_floor(PhysicsWorld *world) {
    RigidBodyDesc floor_desc = rigid_body_desc_default();
    floor_desc.type = BODY_TYPE_STATIC;
    floor_desc.shape = shape_box((vec3){20.0f, 0.5f, 20.0f});
    BodyId floor = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &floor_desc, &floor));
}

/* Spans y in [1.9, 2.5] - clear of a crouched capsule resting on the
 * floor (top at ~1.75) but overlapping a standing one from the same
 * spot (top at ~2.30), so standing up here is blocked while crouching
 * under it is not. */
static void create_low_ceiling(PhysicsWorld *world) {
    RigidBodyDesc ceiling_desc = rigid_body_desc_default();
    ceiling_desc.type = BODY_TYPE_STATIC;
    ceiling_desc.shape = shape_box((vec3){5.0f, 0.3f, 5.0f});
    ceiling_desc.position[1] = 2.2f;
    BodyId ceiling = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &ceiling_desc, &ceiling));
}

static InfantryInputCommand idle_command(void) {
    InfantryInputCommand command = {0};
    return command;
}

/* Camera yaw defaults to 0 at entity creation (camera_create_perspective()),
 * so forward = (1, *, 0) - move_axis_y alone walks along +X. */
static InfantryInputCommand walk_command(float move_axis_y) {
    InfantryInputCommand command = idle_command();
    command.move_axis_y = move_axis_y;
    return command;
}

/* Monotonic across the whole test binary - individual tests only care
 * that it advances, matching sim_clock_tick_index()'s own contract, not
 * about any particular absolute value. No real surface lookup is wired
 * (nullptr) - these tests exercise gait/event production, not surface
 * selection (already covered by outpost_scene_tests/game_audio_tests). */
static void tick(InfantryEntity *entity, InfantryInputCommand command) {
    static uint64_t tick_index = 0;
    infantry_entity_update_fixed(entity, &command, FIXED_DT, tick_index++, nullptr, nullptr);
}

/* character_controller_is_grounded() reflects only the LAST
 * character_controller_move() call - right after creation none has
 * run yet, so it reads false even though the capsule was spawned
 * resting on the floor. One idle tick lets the ground probe run for
 * real before any crouch/jump test logic depends on grounded==true. */
static void settle_grounded(InfantryEntity *entity) {
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(entity);
    for (int i = 0; i < 5 && !character_controller_is_grounded(locomotion->controller); ++i) {
        tick(entity, idle_command());
    }
    TEST_ASSERT_TRUE(character_controller_is_grounded(locomotion->controller));
}

static void crouch_now(InfantryEntity *entity) {
    InfantryInputCommand command = idle_command();
    command.crouch_held = true;
    tick(entity, command);
    TEST_ASSERT_EQUAL(PLAYER_STANCE_CROUCHING, infantry_entity_get_locomotion(entity)->stance);
}

void setUp(void) {
}

void tearDown(void) {
}

static void test_crouched_jump_defers_until_standing_succeeds(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);
    crouch_now(&entity);

    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(&entity);

    /* Jump requested while crouched, in the open (no ceiling): must
     * stand up but must NOT jump on this same tick (points 1 and 5). */
    InfantryInputCommand jump_command = idle_command();
    jump_command.jump_requested = true;
    tick(&entity, jump_command);
    TEST_ASSERT_EQUAL(PLAYER_STANCE_STANDING, locomotion->stance);
    TEST_ASSERT_TRUE(locomotion->vertical_velocity < 1.0f); /* not the jump impulse yet */
    TEST_ASSERT_TRUE(locomotion->stand_to_jump_pending_seconds > 0.0f);

    /* Advance through the approved stand-to-jump transition window
     * (point 4) - the deferred jump must fire before it elapses. */
    bool   jumped = false;
    for (int i = 0; i < 60 && !jumped; ++i) {
        tick(&entity, idle_command());
        if (locomotion->vertical_velocity > 1.0f) {
            jumped = true;
        }
    }
    TEST_ASSERT_TRUE(jumped);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, JUMP_VELOCITY_AFTER_ONE_TICK, locomotion->vertical_velocity);

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_blocked_crouched_jump_is_rejected(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);
    create_low_ceiling(world); /* spans x/z in [-5, 5] */

    /* infantry_entity_create() always spawns STANDING (fixed capsule
     * dims) - spawning directly under the low ceiling would overlap it
     * immediately, which is not the scenario under test. Spawn well
     * clear of the ceiling (x=10), crouch there, then teleport the
     * already-crouched (small) capsule underneath it - exactly the
     * "walked in crouched, now can't stand" case this test means to
     * cover. */
    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){10.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);
    crouch_now(&entity);

    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(&entity);

    vec3 crouched_position;
    character_controller_get_position(locomotion->controller, crouched_position);
    vec3 under_ceiling = {0.0f, crouched_position[1], 0.0f};
    character_controller_set_position(locomotion->controller, under_ceiling);
    /* set_position() doesn't refresh grounded state on its own - one
     * idle tick gets a real probe at the new spot before the jump
     * attempt. */
    tick(&entity, idle_command());
    TEST_ASSERT_TRUE(character_controller_is_grounded(locomotion->controller));
    TEST_ASSERT_EQUAL(PLAYER_STANCE_CROUCHING, locomotion->stance);

    InfantryInputCommand jump_command = idle_command();
    jump_command.jump_requested = true;
    tick(&entity, jump_command);

    /* Standing was blocked by the ceiling (point 3): still crouched,
     * nothing queued, no jump anywhere in sight even after many more
     * ticks with the same blocked geometry. */
    TEST_ASSERT_EQUAL(PLAYER_STANCE_CROUCHING, locomotion->stance);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, locomotion->stand_to_jump_pending_seconds);
    for (int i = 0; i < 60; ++i) {
        tick(&entity, idle_command());
        TEST_ASSERT_TRUE(locomotion->vertical_velocity < 1.0f);
    }
    TEST_ASSERT_EQUAL(PLAYER_STANCE_CROUCHING, locomotion->stance);

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_airborne_crouch_input_is_ignored(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(&entity);

    /* Jump from standing - fires immediately (already standing). */
    InfantryInputCommand jump_command = idle_command();
    jump_command.jump_requested = true;
    tick(&entity, jump_command);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, JUMP_VELOCITY_AFTER_ONE_TICK, locomotion->vertical_velocity);

    /* Hold crouch for a good stretch of airtime (point 6): stance must
     * never change while airborne, however long crouch is held. */
    for (int i = 0; i < 20; ++i) {
        InfantryInputCommand command = idle_command();
        command.crouch_held = true;
        tick(&entity, command);
        TEST_ASSERT_EQUAL(PLAYER_STANCE_STANDING, locomotion->stance);
    }

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_landing_with_crouch_held_crouches_once_grounded(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(&entity);

    InfantryInputCommand jump_command = idle_command();
    jump_command.jump_requested = true;
    tick(&entity, jump_command);

    /* Hold crouch throughout the whole fall, well past a normal jump's
     * airtime, until landing resolves. */
    bool crouched_after_landing = false;
    for (int i = 0; i < 180; ++i) {
        InfantryInputCommand command = idle_command();
        command.crouch_held = true;
        tick(&entity, command);
        if (locomotion->stance == PLAYER_STANCE_CROUCHING) {
            crouched_after_landing = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(crouched_after_landing);
    TEST_ASSERT_TRUE(character_controller_is_grounded(locomotion->controller));

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_jump_cooldown_prevents_immediate_rejump(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(&entity);

    InfantryInputCommand jump_command = idle_command();
    jump_command.jump_requested = true;
    tick(&entity, jump_command);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, JUMP_VELOCITY_AFTER_ONE_TICK, locomotion->vertical_velocity);

    /* Ride it out until landing actually resolves (jump_lockout_seconds_remaining
     * is set to 0.45s at that instant). Waiting on vertical_state
     * reaching LANDING_RECOVERY rather than the raw
     * character_controller_is_grounded() flag: that flag can genuinely
     * (correctly) read true for a tick or two right after a modest
     * jump's takeoff, since the ground probe's fixed reach can still
     * overlap the floor before the capsule has climbed far enough to
     * clear it - not yet a real landing (game logic already accounts
     * for this - see resolve_vertical_state()'s comment on why landing
     * detection also requires vertical_velocity <= 0). */
    for (int i = 0; i < 180 && locomotion->vertical_state != PLAYER_VERTICAL_STATE_LANDING_RECOVERY;
         ++i) {
        tick(&entity, idle_command());
    }
    TEST_ASSERT_EQUAL(PLAYER_VERTICAL_STATE_LANDING_RECOVERY, locomotion->vertical_state);
    TEST_ASSERT_TRUE(character_controller_is_grounded(locomotion->controller));
    TEST_ASSERT_TRUE(locomotion->jump_lockout_seconds_remaining > 0.0f);

    /* Immediately requesting another jump must not fire while locked out. */
    tick(&entity, jump_command);
    TEST_ASSERT_TRUE(locomotion->vertical_velocity < 1.0f);

    /* Wait out the full lockout, then it must fire again. */
    bool rejumped = false;
    for (int i = 0; i < 60 && !rejumped; ++i) {
        tick(&entity, jump_command);
        if (locomotion->vertical_velocity > 1.0f) {
            rejumped = true;
        }
    }
    TEST_ASSERT_TRUE(rejumped);

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_repeated_jump_inputs_do_not_bunny_hop(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(&entity);

    InfantryInputCommand jump_command = idle_command();
    jump_command.jump_requested = true;

    /* First jump fires. */
    tick(&entity, jump_command);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, JUMP_VELOCITY_AFTER_ONE_TICK, locomotion->vertical_velocity);
    const float speed_right_after_first_jump = locomotion->vertical_velocity;

    /* Spec 17.3: the engine's own state machine must not rely solely on
     * one-shot input debouncing - even with jump_requested held true on
     * every single tick while airborne, no second impulse may stack on
     * top of the first. Checked against vertical_state, not the raw
     * character_controller_is_grounded() flag: that flag can
     * legitimately read true for a tick or two right after a modest
     * jump's takeoff (the ground probe's fixed reach can still
     * overlap the floor before the capsule climbs clear of it) even
     * though the entity is still genuinely ascending - vertical_state
     * is what actually gates try_fire_jump(), and is the correct
     * "still airborne" signal to assert against here. */
    for (int i = 0; i < 15; ++i) {
        tick(&entity, jump_command);
        TEST_ASSERT_TRUE(locomotion->vertical_velocity <= speed_right_after_first_jump + 0.01f);
        TEST_ASSERT_NOT_EQUAL(PLAYER_VERTICAL_STATE_GROUNDED, locomotion->vertical_state);
        TEST_ASSERT_NOT_EQUAL(PLAYER_VERTICAL_STATE_LANDING_RECOVERY, locomotion->vertical_state);
    }

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

/* --- M10: Locomotion Foot-Contact Events --- */

static void test_footstep_events_alternate_left_right_while_walking(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);

    InfantryFootContactEvent event;
    PlayerFoot                previous_foot = PLAYER_FOOT_RIGHT; /* sentinel: first must be LEFT */
    int                        events_seen = 0;
    for (int i = 0; i < 300 && events_seen < 6; ++i) {
        tick(&entity, walk_command(1.0f));
        while (infantry_entity_pop_foot_contact_event(&entity, &event)) {
            if (events_seen == 0) {
                TEST_ASSERT_EQUAL(PLAYER_FOOT_LEFT, event.foot);
            } else {
                TEST_ASSERT_NOT_EQUAL(previous_foot, event.foot);
            }
            previous_foot = event.foot;
            events_seen += 1;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL(6, events_seen);

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_footstep_event_popped_exactly_once(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);

    InfantryFootContactEvent event;
    bool                      any_event = false;
    for (int i = 0; i < 60 && !any_event; ++i) {
        tick(&entity, walk_command(1.0f));
        any_event = infantry_entity_pop_foot_contact_event(&entity, &event);
    }
    TEST_ASSERT_TRUE(any_event);

    /* Queue must not still hold (or re-yield) the same event - draining
     * to empty means every further pop returns false until a new tick
     * produces a new one. */
    InfantryFootContactEvent drain_event;
    while (infantry_entity_pop_foot_contact_event(&entity, &drain_event)) {
        /* drain any remaining queued events from the same walk */
    }
    TEST_ASSERT_FALSE(infantry_entity_pop_foot_contact_event(&entity, &drain_event));

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_no_footstep_events_while_airborne(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(&entity);

    InfantryInputCommand jump_and_walk = walk_command(1.0f);
    jump_and_walk.jump_requested = true;
    tick(&entity, jump_and_walk); /* fires the jump this same tick */

    InfantryFootContactEvent event;
    int                       ticks_checked = 0;
    while (locomotion->vertical_state != PLAYER_VERTICAL_STATE_GROUNDED &&
           locomotion->vertical_state != PLAYER_VERTICAL_STATE_LANDING_RECOVERY && ticks_checked < 120) {
        tick(&entity, walk_command(1.0f));
        TEST_ASSERT_FALSE(infantry_entity_pop_foot_contact_event(&entity, &event));
        ticks_checked += 1;
    }
    TEST_ASSERT_TRUE(ticks_checked > 0);

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_no_footstep_events_walking_into_wall(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    /* Thin wall immediately in front of the spawn point (+X, matching
     * walk_command()'s direction) - the capsule contacts it almost
     * immediately and collide-and-slide yields ~0 further planar
     * displacement into it. */
    RigidBodyDesc wall_desc = rigid_body_desc_default();
    wall_desc.type = BODY_TYPE_STATIC;
    wall_desc.shape = shape_box((vec3){0.2f, 2.0f, 5.0f});
    wall_desc.position[0] = 0.6f;
    wall_desc.position[1] = 1.5f;
    BodyId wall = PHYSICS_INVALID_BODY_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, rigid_body_create(world, &wall_desc, &wall));

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);

    InfantryFootContactEvent event;
    for (int i = 0; i < 300; ++i) {
        tick(&entity, walk_command(1.0f));
        TEST_ASSERT_FALSE(infantry_entity_pop_foot_contact_event(&entity, &event));
    }

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_first_contact_after_reset_gait_is_left(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);

    /* Walk far enough that the phase has clearly advanced past LEFT
     * (several alternations), so the reset below is what makes the
     * NEXT assertion true - not coincidence. */
    InfantryFootContactEvent event;
    int                       events_seen = 0;
    for (int i = 0; i < 300 && events_seen < 4; ++i) {
        tick(&entity, walk_command(1.0f));
        while (infantry_entity_pop_foot_contact_event(&entity, &event)) {
            events_seen += 1;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL(4, events_seen);

    infantry_entity_reset_gait(&entity);

    bool       first_found = false;
    PlayerFoot first_foot = PLAYER_FOOT_RIGHT;
    for (int i = 0; i < 120 && !first_found; ++i) {
        tick(&entity, walk_command(1.0f));
        if (infantry_entity_pop_foot_contact_event(&entity, &event)) {
            first_foot = event.foot;
            first_found = true;
        }
    }
    TEST_ASSERT_TRUE(first_found);
    TEST_ASSERT_EQUAL(PLAYER_FOOT_LEFT, first_foot);

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_teleport_reset_prevents_stale_stride_from_carrying_over(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(&entity);

    const PlayerGait  gait = {.stance = PLAYER_STANCE_STANDING, .speed_tier = PLAYER_SPEED_TIER_RUN};
    const float       stride_length = player_movement_stride_length_for_gait(gait);
    locomotion->gait.stride.accumulated_meters = stride_length * 0.95f;

    /* Teleport (bypassing normal movement entirely) then reset gait -
     * exactly what debug_overlay.c and main.c's smoke-test branch do. */
    vec3 elsewhere = {50.0f, 1.4f, 50.0f};
    character_controller_set_position(locomotion->controller, elsewhere);
    glm_vec3_copy(elsewhere, infantry_entity_get_transform(&entity)->position);
    infantry_entity_reset_gait(&entity);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, locomotion->gait.stride.accumulated_meters);

    InfantryFootContactEvent event;
    TEST_ASSERT_FALSE(infantry_entity_pop_foot_contact_event(&entity, &event));

    /* set_position() doesn't refresh grounded state on its own (see
     * settle_grounded()'s own comment). */
    tick(&entity, idle_command());

    /* A short walk - well under one full stride on its own - must not
     * produce an event. Without the reset above, the stale 0.95x
     * accumulation plus this same movement would have crossed the
     * stride threshold and fired. */
    bool any_event = false;
    for (int i = 0; i < 8; ++i) {
        tick(&entity, walk_command(1.0f));
        if (infantry_entity_pop_foot_contact_event(&entity, &event)) {
            any_event = true;
        }
    }
    TEST_ASSERT_FALSE(any_event);

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_footstep_cadence_differs_by_gait(void) {
    const int ticks_per_trial = 180; /* 3 real seconds - stays well within the sprint stamina budget */

    PhysicsWorld *crouch_world = nullptr, *run_world = nullptr, *sprint_world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &crouch_world));
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &run_world));
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &sprint_world));
    create_floor(crouch_world);
    create_floor(run_world);
    create_floor(sprint_world);

    InfantryEntity crouch_entity, run_entity, sprint_entity;
    TEST_ASSERT_EQUAL(RESULT_OK, infantry_entity_create(crouch_world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f,
                                                          &crouch_entity));
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(run_world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &run_entity));
    TEST_ASSERT_EQUAL(RESULT_OK, infantry_entity_create(sprint_world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f,
                                                          &sprint_entity));
    settle_grounded(&crouch_entity);
    settle_grounded(&run_entity);
    settle_grounded(&sprint_entity);
    crouch_now(&crouch_entity);

    InfantryFootContactEvent event;
    uint32_t                 crouch_count = 0, run_count = 0, sprint_count = 0;

    for (int i = 0; i < ticks_per_trial; ++i) {
        /* crouch_held is re-evaluated every tick (not sticky) - must
         * stay held, or resolve_stance() stands the entity back up
         * after crouch_now()'s one tick. */
        InfantryInputCommand command = walk_command(1.0f);
        command.crouch_held = true;
        tick(&crouch_entity, command);
        while (infantry_entity_pop_foot_contact_event(&crouch_entity, &event)) {
            crouch_count += 1;
        }
    }
    for (int i = 0; i < ticks_per_trial; ++i) {
        tick(&run_entity, walk_command(1.0f));
        while (infantry_entity_pop_foot_contact_event(&run_entity, &event)) {
            run_count += 1;
        }
    }
    for (int i = 0; i < ticks_per_trial; ++i) {
        InfantryInputCommand command = walk_command(1.0f);
        command.sprint_held = true;
        tick(&sprint_entity, command);
        while (infantry_entity_pop_foot_contact_event(&sprint_entity, &event)) {
            sprint_count += 1;
        }
    }

    TEST_ASSERT_TRUE(crouch_count > 0);
    TEST_ASSERT_TRUE(crouch_count < run_count);
    TEST_ASSERT_TRUE(run_count < sprint_count);

    infantry_entity_destroy(&crouch_entity);
    infantry_entity_destroy(&run_entity);
    infantry_entity_destroy(&sprint_entity);
    physics_world_destroy(crouch_world);
    physics_world_destroy(run_world);
    physics_world_destroy(sprint_world);
}

static void test_stop_and_restart_reengages_left_foot(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);

    InfantryFootContactEvent event;
    int                       events_seen = 0;
    for (int i = 0; i < 300 && events_seen < 3; ++i) {
        tick(&entity, walk_command(1.0f));
        while (infantry_entity_pop_foot_contact_event(&entity, &event)) {
            events_seen += 1;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL(3, events_seen);

    /* Stand still past PLAYER_GAIT_IDLE_RESET_SECONDS - releasing input
     * decelerates horizontal_velocity toward 0 over the tier's own
     * decel constant first (well under a second from Run speed), and
     * only THEN does the idle timer start counting; 90 ticks (1.5s)
     * comfortably covers both. */
    for (int i = 0; i < 90; ++i) {
        tick(&entity, idle_command());
    }

    /* The very first new contact after resuming must be LEFT again,
     * regardless of whatever parity would otherwise have continued. */
    bool       first_found = false;
    PlayerFoot first_foot = PLAYER_FOOT_RIGHT;
    for (int i = 0; i < 120 && !first_found; ++i) {
        tick(&entity, walk_command(1.0f));
        if (infantry_entity_pop_foot_contact_event(&entity, &event)) {
            first_foot = event.foot;
            first_found = true;
        }
    }
    TEST_ASSERT_TRUE(first_found);
    TEST_ASSERT_EQUAL(PLAYER_FOOT_LEFT, first_foot);

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_crouch_stationary_produces_no_events(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);
    crouch_now(&entity);

    InfantryFootContactEvent event;
    for (int i = 0; i < 60; ++i) {
        InfantryInputCommand command = idle_command();
        command.crouch_held = true;
        tick(&entity, command);
        TEST_ASSERT_FALSE(infantry_entity_pop_foot_contact_event(&entity, &event));
    }

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_landing_does_not_produce_duplicate_ordinary_footstep(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(&entity);

    InfantryInputCommand jump_and_walk = walk_command(1.0f);
    jump_and_walk.jump_requested = true;
    tick(&entity, jump_and_walk);

    InfantryFootContactEvent event;
    bool                      saw_landing_tick = false;
    bool                      was_landing_recovery = false;
    for (int i = 0; i < 180; ++i) {
        tick(&entity, walk_command(1.0f));
        bool got_event = false;
        while (infantry_entity_pop_foot_contact_event(&entity, &event)) {
            got_event = true;
        }
        if (!was_landing_recovery && locomotion->vertical_state == PLAYER_VERTICAL_STATE_LANDING_RECOVERY) {
            TEST_ASSERT_FALSE(got_event);
            saw_landing_tick = true;
            was_landing_recovery = true;
        }
    }
    TEST_ASSERT_TRUE(saw_landing_tick);

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_queue_overflow_drops_oldest_and_counts_it(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(&entity);

    /* Never drained - walk long enough to produce more events than the
     * queue can hold. */
    for (int i = 0; i < 600; ++i) {
        tick(&entity, walk_command(1.0f));
    }
    TEST_ASSERT_EQUAL(INFANTRY_FOOT_CONTACT_EVENT_CAPACITY, locomotion->gait.queue_count);
    TEST_ASSERT_TRUE(locomotion->gait.dropped_event_count > 0);

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_large_single_tick_displacement_caps_and_drops(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(&entity);

    /* Not reachable through normal input/physics yet (no knockback or
     * launcher system) - forcing it directly proves the safety cap
     * holds regardless, per the M10 requirement that it must. */
    locomotion->horizontal_velocity[0] = 500.0f;
    locomotion->horizontal_velocity[2] = 0.0f;
    tick(&entity, idle_command());

    uint32_t                  events_this_tick = 0;
    InfantryFootContactEvent  event;
    while (infantry_entity_pop_foot_contact_event(&entity, &event)) {
        events_this_tick += 1;
    }
    TEST_ASSERT_EQUAL(PLAYER_MAX_FOOT_CONTACTS_PER_TICK, events_this_tick);
    TEST_ASSERT_TRUE(locomotion->gait.dropped_contact_count > 0);

    /* Force the velocity back to a real stop rather than relying on
     * decel to bring 500 m/s down naturally (that alone would take
     * ~59 real seconds at PLAYER_RUN_DECEL_METERS_PER_SEC2 - several
     * more ticks would still see a huge, still-capped displacement). */
    locomotion->horizontal_velocity[0] = 0.0f;
    locomotion->horizontal_velocity[2] = 0.0f;

    /* No delayed burst afterward: standing still (no new realized
     * movement) must not surface any more contacts, even though a huge
     * distance was "spent" (accounted for, not carried forward) that
     * tick. */
    for (int i = 0; i < 30; ++i) {
        tick(&entity, idle_command());
        TEST_ASSERT_FALSE(infantry_entity_pop_foot_contact_event(&entity, &event));
    }

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_deterministic_replay_same_input_same_events(void) {
    PhysicsWorld *world_a = nullptr, *world_b = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world_a));
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world_b));
    create_floor(world_a);
    create_floor(world_b);

    InfantryEntity entity_a, entity_b;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world_a, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity_a));
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world_b, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity_b));
    settle_grounded(&entity_a);
    settle_grounded(&entity_b);

    PlayerFoot feet_a[64];
    uint32_t   count_a = 0;
    PlayerFoot feet_b[64];
    uint32_t   count_b = 0;
    InfantryFootContactEvent event;

    for (int i = 0; i < 200; ++i) {
        tick(&entity_a, walk_command(1.0f));
        while (count_a < 64 && infantry_entity_pop_foot_contact_event(&entity_a, &event)) {
            feet_a[count_a++] = event.foot;
        }
    }
    for (int i = 0; i < 200; ++i) {
        tick(&entity_b, walk_command(1.0f));
        while (count_b < 64 && infantry_entity_pop_foot_contact_event(&entity_b, &event)) {
            feet_b[count_b++] = event.foot;
        }
    }

    TEST_ASSERT_EQUAL(count_a, count_b);
    TEST_ASSERT_TRUE(count_a > 0);
    for (uint32_t i = 0; i < count_a; ++i) {
        TEST_ASSERT_EQUAL(feet_a[i], feet_b[i]);
    }

    infantry_entity_destroy(&entity_a);
    infantry_entity_destroy(&entity_b);
    physics_world_destroy(world_a);
    physics_world_destroy(world_b);
}

/* --- M11: landing event queue --- */

static void test_landing_event_popped_exactly_once(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);

    InfantryInputCommand jump_command = idle_command();
    jump_command.jump_requested = true;
    tick(&entity, jump_command);

    InfantryFallLandingEvent landing;
    bool                      got_landing = false;
    for (int i = 0; i < 180 && !got_landing; ++i) {
        tick(&entity, idle_command());
        got_landing = infantry_entity_pop_landing_event(&entity, &landing);
    }
    TEST_ASSERT_TRUE(got_landing);
    /* Drained - a further pop the same tick (nothing new produced)
     * returns false. */
    TEST_ASSERT_FALSE(infantry_entity_pop_landing_event(&entity, &landing));

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

static void test_landing_queue_overflow_drops_oldest_and_counts_it(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(&entity);

    /* Never drained - repeated jump/land cycles until more landings
     * than INFANTRY_LANDING_EVENT_QUEUE_CAPACITY have occurred. */
    for (int cycle = 0; cycle < 6; ++cycle) {
        InfantryInputCommand jump_command = idle_command();
        jump_command.jump_requested = true;
        tick(&entity, jump_command);
        for (int i = 0; i < 120 && locomotion->vertical_state != PLAYER_VERTICAL_STATE_LANDING_RECOVERY;
             ++i) {
            tick(&entity, idle_command());
        }
        for (int i = 0; i < 60 && locomotion->vertical_state != PLAYER_VERTICAL_STATE_GROUNDED; ++i) {
            tick(&entity, idle_command());
        }
        /* jump_lockout_seconds_remaining (0.45s) outlasts
         * landing_recovery_seconds_remaining (<=0.35s) - without this
         * extra margin the next cycle's jump can silently fail against
         * residual lockout, undercounting landings. */
        for (int i = 0; i < 30 && locomotion->jump_lockout_seconds_remaining > 0.0f; ++i) {
            tick(&entity, idle_command());
        }
    }

    TEST_ASSERT_EQUAL(INFANTRY_LANDING_EVENT_QUEUE_CAPACITY, locomotion->landing_queue.queue_count);
    TEST_ASSERT_TRUE(locomotion->landing_queue.dropped_event_count > 0);

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

/* --- M11: camera bob phase must stay coherent with M10's own emitted
 * L/R events - PlayerGaitStrideState (read via
 * infantry_entity_get_gait_presentation_snapshot()) is the only
 * odometer; this proves camera_motion's pure bob curve never disagrees
 * with what M10 itself just fired, across ordinary walking, a gait
 * change, and an explicit reset. */
static void test_camera_foot_phase_extrema_coherent_with_contact_events(void) {
    PhysicsWorld *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(nullptr, &world));
    create_floor(world);

    InfantryEntity entity;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       infantry_entity_create(world, (vec3){0.0f, 1.4f, 0.0f}, 16.0f / 9.0f, &entity));
    settle_grounded(&entity);

    InfantryFootContactEvent event;

    /* Ordinary walking. */
    int contacts_checked = 0;
    for (int i = 0; i < 300 && contacts_checked < 4; ++i) {
        PlayerGaitPresentationSnapshot snapshot_before;
        infantry_entity_get_gait_presentation_snapshot(&entity, &snapshot_before);
        const PlayerFoot pending_foot = snapshot_before.foot_phase;

        tick(&entity, walk_command(1.0f));

        while (infantry_entity_pop_foot_contact_event(&entity, &event)) {
            TEST_ASSERT_EQUAL(pending_foot, event.foot);
            const float extremum = camera_motion_gait_lateral_offset(pending_foot, 0.5f, 1.0f);
            if (pending_foot == PLAYER_FOOT_LEFT) {
                TEST_ASSERT_TRUE(extremum < 0.0f);
            } else {
                TEST_ASSERT_TRUE(extremum > 0.0f);
            }
            contacts_checked += 1;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL(4, contacts_checked);

    /* Across a gait change (Walk -> Sprint mid-stride). */
    contacts_checked = 0;
    for (int i = 0; i < 300 && contacts_checked < 4; ++i) {
        PlayerGaitPresentationSnapshot snapshot_before;
        infantry_entity_get_gait_presentation_snapshot(&entity, &snapshot_before);
        const PlayerFoot pending_foot = snapshot_before.foot_phase;

        InfantryInputCommand command = walk_command(1.0f);
        command.sprint_held = true;
        tick(&entity, command);

        while (infantry_entity_pop_foot_contact_event(&entity, &event)) {
            TEST_ASSERT_EQUAL(pending_foot, event.foot);
            contacts_checked += 1;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL(4, contacts_checked);

    /* After an explicit reset - phase snaps to LEFT, and coherence
     * continues to hold for the strides that follow. */
    infantry_entity_reset_gait(&entity);
    PlayerGaitPresentationSnapshot snapshot_after_reset;
    infantry_entity_get_gait_presentation_snapshot(&entity, &snapshot_after_reset);
    TEST_ASSERT_EQUAL(PLAYER_FOOT_LEFT, snapshot_after_reset.foot_phase);

    contacts_checked = 0;
    for (int i = 0; i < 300 && contacts_checked < 2; ++i) {
        PlayerGaitPresentationSnapshot snapshot_before;
        infantry_entity_get_gait_presentation_snapshot(&entity, &snapshot_before);
        const PlayerFoot pending_foot = snapshot_before.foot_phase;

        tick(&entity, walk_command(1.0f));

        while (infantry_entity_pop_foot_contact_event(&entity, &event)) {
            TEST_ASSERT_EQUAL(pending_foot, event.foot);
            contacts_checked += 1;
        }
    }
    TEST_ASSERT_GREATER_OR_EQUAL(2, contacts_checked);

    infantry_entity_destroy(&entity);
    physics_world_destroy(world);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_crouched_jump_defers_until_standing_succeeds);
    RUN_TEST(test_blocked_crouched_jump_is_rejected);
    RUN_TEST(test_airborne_crouch_input_is_ignored);
    RUN_TEST(test_landing_with_crouch_held_crouches_once_grounded);
    RUN_TEST(test_jump_cooldown_prevents_immediate_rejump);
    RUN_TEST(test_repeated_jump_inputs_do_not_bunny_hop);

    RUN_TEST(test_footstep_events_alternate_left_right_while_walking);
    RUN_TEST(test_footstep_event_popped_exactly_once);
    RUN_TEST(test_no_footstep_events_while_airborne);
    RUN_TEST(test_no_footstep_events_walking_into_wall);
    RUN_TEST(test_first_contact_after_reset_gait_is_left);
    RUN_TEST(test_teleport_reset_prevents_stale_stride_from_carrying_over);
    RUN_TEST(test_footstep_cadence_differs_by_gait);
    RUN_TEST(test_stop_and_restart_reengages_left_foot);
    RUN_TEST(test_crouch_stationary_produces_no_events);
    RUN_TEST(test_landing_does_not_produce_duplicate_ordinary_footstep);
    RUN_TEST(test_queue_overflow_drops_oldest_and_counts_it);
    RUN_TEST(test_large_single_tick_displacement_caps_and_drops);
    RUN_TEST(test_deterministic_replay_same_input_same_events);

    RUN_TEST(test_landing_event_popped_exactly_once);
    RUN_TEST(test_landing_queue_overflow_drops_oldest_and_counts_it);
    RUN_TEST(test_camera_foot_phase_extrema_coherent_with_contact_events);

    return UNITY_END();
}
