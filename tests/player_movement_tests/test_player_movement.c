/*
 * player_movement is pure math/logic (no Camera, Physics, or Input
 * dependency) - these tests exercise it directly with no engine
 * running at all.
 */
#include "player_movement.h"
#include "player_tuning.h"

#include "unity.h"

#include <math.h>

void setUp(void) {
}

void tearDown(void) {
}

static void test_stamina_drains_while_sprinting_and_locks_out_at_zero(void) {
    StaminaState state = {.stamina = PLAYER_STAMINA_MAX, .sprint_locked_out = false};

    state = player_movement_update_stamina(state, true, true, true, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, PLAYER_STAMINA_MAX - PLAYER_STAMINA_DRAIN_PER_SEC, state.stamina);
    TEST_ASSERT_FALSE(state.sprint_locked_out);

    /* Drive it well past zero - must clamp, not go negative. */
    state = player_movement_update_stamina(state, true, true, true, 10.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, state.stamina);
    TEST_ASSERT_TRUE(state.sprint_locked_out);
}

static void test_locked_out_sprint_stays_off_below_reenable_threshold(void) {
    StaminaState state = {.stamina = 0.0f, .sprint_locked_out = true};

    /* Regen a little, but stay below the reenable threshold. */
    state = player_movement_update_stamina(state, true, true, true, 0.1f);
    TEST_ASSERT_TRUE(state.stamina < PLAYER_STAMINA_SPRINT_REENABLE_THRESHOLD);
    TEST_ASSERT_TRUE(state.sprint_locked_out);

    const PlayerSpeedTier tier =
        player_movement_select_speed_tier(true, false, state.sprint_locked_out, true, true);
    TEST_ASSERT_EQUAL(PLAYER_SPEED_TIER_RUN, tier);
}

static void test_stamina_regenerates_and_clears_lockout_exactly_at_threshold(void) {
    StaminaState state = {.stamina = 0.0f, .sprint_locked_out = true};

    const float seconds_to_threshold =
        PLAYER_STAMINA_SPRINT_REENABLE_THRESHOLD / PLAYER_STAMINA_REGEN_PER_SEC;

    /* Just short of the threshold: still locked out. */
    state = player_movement_update_stamina(state, false, false, false, seconds_to_threshold - 0.05f);
    TEST_ASSERT_TRUE(state.sprint_locked_out);

    /* Crossing the threshold clears the lockout. */
    state = player_movement_update_stamina(state, false, false, false, 0.1f);
    TEST_ASSERT_TRUE(state.stamina >= PLAYER_STAMINA_SPRINT_REENABLE_THRESHOLD);
    TEST_ASSERT_FALSE(state.sprint_locked_out);
}

static void test_holding_sprint_while_standing_still_costs_nothing(void) {
    StaminaState state = {.stamina = PLAYER_STAMINA_MAX, .sprint_locked_out = false};

    state = player_movement_update_stamina(state, true, false, true, 5.0f);
    TEST_ASSERT_EQUAL_FLOAT(PLAYER_STAMINA_MAX, state.stamina);

    const PlayerSpeedTier tier = player_movement_select_speed_tier(true, false, false, false, true);
    TEST_ASSERT_EQUAL(PLAYER_SPEED_TIER_RUN, tier);
}

static void test_sprint_context_disallowed_falls_back_without_draining(void) {
    /* Airborne or past the sprint-cone grace period: sprint_context_allowed
     * is false even though sprint is held and there is move input -
     * must not drain stamina, and tier must not be Sprint. */
    StaminaState state = {.stamina = PLAYER_STAMINA_MAX, .sprint_locked_out = false};
    state = player_movement_update_stamina(state, true, true, false, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(PLAYER_STAMINA_MAX, state.stamina);

    const PlayerSpeedTier tier = player_movement_select_speed_tier(true, false, false, true, false);
    TEST_ASSERT_NOT_EQUAL(PLAYER_SPEED_TIER_SPRINT, tier);
}

static void test_select_speed_tier_defaults_to_run(void) {
    const PlayerSpeedTier tier = player_movement_select_speed_tier(false, false, false, true, true);
    TEST_ASSERT_EQUAL(PLAYER_SPEED_TIER_RUN, tier);
}

static void test_select_speed_tier_walk_modifier_overrides_default(void) {
    const PlayerSpeedTier tier = player_movement_select_speed_tier(false, true, false, true, true);
    TEST_ASSERT_EQUAL(PLAYER_SPEED_TIER_WALK, tier);
}

static void test_select_speed_tier_sprint_wins_over_walk_modifier(void) {
    const PlayerSpeedTier tier = player_movement_select_speed_tier(true, true, false, true, true);
    TEST_ASSERT_EQUAL(PLAYER_SPEED_TIER_SPRINT, tier);
}

static void test_speed_accel_decel_for_tier_are_distinct_per_gait(void) {
    TEST_ASSERT_EQUAL_FLOAT(PLAYER_WALK_SPEED_METERS_PER_SEC,
                             player_movement_speed_for_tier(PLAYER_SPEED_TIER_WALK));
    TEST_ASSERT_EQUAL_FLOAT(PLAYER_RUN_SPEED_METERS_PER_SEC,
                             player_movement_speed_for_tier(PLAYER_SPEED_TIER_RUN));
    TEST_ASSERT_EQUAL_FLOAT(PLAYER_SPRINT_SPEED_METERS_PER_SEC,
                             player_movement_speed_for_tier(PLAYER_SPEED_TIER_SPRINT));

    /* Accel is strongest at Walk, weakest at Sprint (spec 8). */
    TEST_ASSERT_TRUE(player_movement_accel_for_tier(PLAYER_SPEED_TIER_WALK) >
                      player_movement_accel_for_tier(PLAYER_SPEED_TIER_RUN));
    TEST_ASSERT_TRUE(player_movement_accel_for_tier(PLAYER_SPEED_TIER_RUN) >
                      player_movement_accel_for_tier(PLAYER_SPEED_TIER_SPRINT));

    /* Braking is strongest at Walk, weakest at Sprint (spec 9). */
    TEST_ASSERT_TRUE(player_movement_decel_for_tier(PLAYER_SPEED_TIER_WALK) >
                      player_movement_decel_for_tier(PLAYER_SPEED_TIER_RUN));
    TEST_ASSERT_TRUE(player_movement_decel_for_tier(PLAYER_SPEED_TIER_RUN) >
                      player_movement_decel_for_tier(PLAYER_SPEED_TIER_SPRINT));
}

static void test_stopping_distances_land_within_spec_target_ranges(void) {
    /* d = v^2 / (2*decel), constant-deceleration stopping distance. */
    const float walk_speed = player_movement_speed_for_tier(PLAYER_SPEED_TIER_WALK);
    const float walk_decel = player_movement_decel_for_tier(PLAYER_SPEED_TIER_WALK);
    const float walk_distance = (walk_speed * walk_speed) / (2.0f * walk_decel);
    TEST_ASSERT_TRUE(walk_distance >= 0.08f && walk_distance <= 0.16f);

    const float run_speed = player_movement_speed_for_tier(PLAYER_SPEED_TIER_RUN);
    const float run_decel = player_movement_decel_for_tier(PLAYER_SPEED_TIER_RUN);
    const float run_distance = (run_speed * run_speed) / (2.0f * run_decel);
    TEST_ASSERT_TRUE(run_distance >= 0.35f && run_distance <= 0.65f);

    const float sprint_speed = player_movement_speed_for_tier(PLAYER_SPEED_TIER_SPRINT);
    const float sprint_decel = player_movement_decel_for_tier(PLAYER_SPEED_TIER_SPRINT);
    const float sprint_distance = (sprint_speed * sprint_speed) / (2.0f * sprint_decel);
    TEST_ASSERT_TRUE(sprint_distance >= 1.05f && sprint_distance <= 1.55f);
}

static void test_approach_velocity_reaches_target_without_overshoot(void) {
    vec3 current = {0.0f, 0.0f, 0.0f};
    vec3 target = {8.0f, 0.0f, 0.0f};
    vec3 result;

    /* accel=40, dt=0.1 -> max_step=4, well short of the 8-unit gap. */
    player_movement_approach_velocity(current, target, 40.0f, 0.1f, result);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 4.0f, result[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, result[1]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, result[2]);

    /* A large dt must clamp exactly to target, never overshoot. */
    player_movement_approach_velocity(current, target, 40.0f, 10.0f, result);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 8.0f, result[0]);
}

static void test_approach_velocity_decelerates_to_zero(void) {
    vec3 current = {8.0f, 0.0f, 0.0f};
    vec3 target = {0.0f, 0.0f, 0.0f};
    vec3 result;

    player_movement_approach_velocity(current, target, 50.0f, 10.0f, result);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, result[0]);
}

static void test_wrap_angle_radians_stays_in_range(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, player_movement_wrap_angle_radians(0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -(GLM_PIf - 0.1f),
                              player_movement_wrap_angle_radians(GLM_PIf + 0.1f));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, GLM_PIf - 0.1f,
                              player_movement_wrap_angle_radians(-GLM_PIf - 0.1f));
}

static void test_angle_difference_takes_the_short_way_around(void) {
    /* From 170 degrees to -170 degrees: the short way is +20 degrees
     * (through 180), not -340. */
    const float from = glm_rad(170.0f);
    const float to = glm_rad(-170.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, glm_rad(20.0f), player_movement_angle_difference_radians(from, to));

    /* From 0 to 200 degrees: the short way is -160 degrees. */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, glm_rad(-160.0f),
                              player_movement_angle_difference_radians(0.0f, glm_rad(200.0f)));
}

static void test_yaw_from_direction_matches_camera_convention(void) {
    /* Camera.yaw_radians convention: forward = (cos(yaw), *, sin(yaw)) -
     * see camera.c's camera_get_forward(). */
    vec3 along_positive_x = {1.0f, 0.0f, 0.0f};
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, player_movement_yaw_from_direction(along_positive_x));

    vec3 along_positive_z = {0.0f, 0.0f, 1.0f};
    TEST_ASSERT_FLOAT_WITHIN(0.001f, GLM_PI_2f, player_movement_yaw_from_direction(along_positive_z));
}

static void test_resolve_body_yaw_follows_movement_direction_while_moving(void) {
    const float result = player_movement_resolve_body_yaw(0.0f, glm_rad(90.0f), glm_rad(45.0f), true,
                                                            PLAYER_TURN_IN_PLACE_THRESHOLD_RADIANS);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, glm_rad(45.0f), result);
}

static void test_resolve_body_yaw_holds_while_stationary_within_threshold(void) {
    /* 30 degrees of view/body divergence, well under the 65 degree
     * threshold - body must not turn in place yet. */
    const float result = player_movement_resolve_body_yaw(0.0f, glm_rad(30.0f), 0.0f, false,
                                                            PLAYER_TURN_IN_PLACE_THRESHOLD_RADIANS);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, result);
}

static void test_resolve_body_yaw_turns_in_place_beyond_threshold(void) {
    /* 90 degrees of divergence, past the 65 degree threshold - body
     * snaps to match view yaw. */
    const float result = player_movement_resolve_body_yaw(0.0f, glm_rad(90.0f), 0.0f, false,
                                                            PLAYER_TURN_IN_PLACE_THRESHOLD_RADIANS);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, glm_rad(90.0f), result);
}

static void test_is_within_cone_accepts_and_rejects_correctly(void) {
    vec3 forward = {1.0f, 0.0f, 0.0f};

    vec3 straight_ahead = {1.0f, 0.0f, 0.0f};
    TEST_ASSERT_TRUE(player_movement_is_within_cone(forward, straight_ahead,
                                                     PLAYER_SPRINT_CONE_HALF_ANGLE_RADIANS));

    /* 45 degrees off forward: inside the 55 degree cone. */
    vec3 diagonal = {cosf(glm_rad(45.0f)), 0.0f, sinf(glm_rad(45.0f))};
    TEST_ASSERT_TRUE(
        player_movement_is_within_cone(forward, diagonal, PLAYER_SPRINT_CONE_HALF_ANGLE_RADIANS));

    /* Straight sideways (90 degrees): outside the 55 degree cone. */
    vec3 sideways = {0.0f, 0.0f, 1.0f};
    TEST_ASSERT_FALSE(
        player_movement_is_within_cone(forward, sideways, PLAYER_SPRINT_CONE_HALF_ANGLE_RADIANS));
}

static void test_classify_turn_from_a_stop_is_always_minor(void) {
    vec3 stationary = {0.0f, 0.0f, 0.0f};
    vec3 any_direction = {0.0f, 0.0f, 1.0f};
    TEST_ASSERT_EQUAL(PLAYER_TURN_SEVERITY_MINOR, player_movement_classify_turn(stationary, any_direction));
}

static void test_classify_turn_severity_by_angle(void) {
    vec3 current_velocity = {5.0f, 0.0f, 0.0f}; /* moving along +X */

    vec3 slight = {cosf(glm_rad(10.0f)), 0.0f, sinf(glm_rad(10.0f))};
    TEST_ASSERT_EQUAL(PLAYER_TURN_SEVERITY_MINOR, player_movement_classify_turn(current_velocity, slight));

    vec3 moderate = {cosf(glm_rad(50.0f)), 0.0f, sinf(glm_rad(50.0f))};
    TEST_ASSERT_EQUAL(PLAYER_TURN_SEVERITY_MODERATE,
                       player_movement_classify_turn(current_velocity, moderate));

    vec3 sharp = {cosf(glm_rad(90.0f)), 0.0f, sinf(glm_rad(90.0f))};
    TEST_ASSERT_EQUAL(PLAYER_TURN_SEVERITY_SHARP, player_movement_classify_turn(current_velocity, sharp));

    vec3 reversal = {-1.0f, 0.0f, 0.0f};
    TEST_ASSERT_EQUAL(PLAYER_TURN_SEVERITY_REVERSAL,
                       player_movement_classify_turn(current_velocity, reversal));
}

static void test_turn_speed_multiplier_decreases_with_severity(void) {
    TEST_ASSERT_EQUAL_FLOAT(1.0f, player_movement_turn_speed_multiplier(PLAYER_TURN_SEVERITY_MINOR));
    TEST_ASSERT_TRUE(player_movement_turn_speed_multiplier(PLAYER_TURN_SEVERITY_MODERATE) < 1.0f);
    TEST_ASSERT_TRUE(player_movement_turn_speed_multiplier(PLAYER_TURN_SEVERITY_SHARP) <
                      player_movement_turn_speed_multiplier(PLAYER_TURN_SEVERITY_MODERATE));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, player_movement_turn_speed_multiplier(PLAYER_TURN_SEVERITY_REVERSAL));
}

static void test_clamp_sprint_turn_passes_through_from_a_stop(void) {
    vec3 stationary = {0.0f, 0.0f, 0.0f};
    vec3 wish = {0.0f, 0.0f, 1.0f};
    vec3 result;
    const bool exceeded = player_movement_clamp_sprint_turn(
        stationary, wish, PLAYER_SPRINT_SPEED_METERS_PER_SEC, 0.1f, result);
    TEST_ASSERT_FALSE(exceeded);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, wish[2], result[2]);
}

static void test_clamp_sprint_turn_limits_a_sharp_redirect_at_full_speed(void) {
    /* Moving at full sprint speed along +X, wishing to go straight to
     * +Z (90 degrees) in one tick - the 55 deg/s full-speed turn rate
     * cannot cover 90 degrees in a small dt, so this must clamp and
     * report exceeded. */
    vec3 current_velocity = {PLAYER_SPRINT_SPEED_METERS_PER_SEC, 0.0f, 0.0f};
    vec3 wish = {0.0f, 0.0f, 1.0f};
    vec3 result;
    const bool exceeded = player_movement_clamp_sprint_turn(
        current_velocity, wish, PLAYER_SPRINT_SPEED_METERS_PER_SEC, 0.05f, result);
    TEST_ASSERT_TRUE(exceeded);

    /* Result must still be a unit vector rotated only partway toward
     * +Z, not the full 90 degrees. */
    const float resulting_yaw = player_movement_yaw_from_direction(result);
    TEST_ASSERT_TRUE(fabsf(resulting_yaw) < glm_rad(90.0f));
}

static void test_slope_angle_radians_zero_for_flat_ground(void) {
    vec3 up = {0.0f, 1.0f, 0.0f};
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, player_movement_slope_angle_radians(up));
}

static void test_slope_angle_radians_matches_a_known_tilt(void) {
    /* Normal tilted 30 degrees from vertical toward +X. */
    vec3 tilted = {sinf(glm_rad(30.0f)), cosf(glm_rad(30.0f)), 0.0f};
    TEST_ASSERT_FLOAT_WITHIN(0.01f, glm_rad(30.0f), player_movement_slope_angle_radians(tilted));
}

static void test_is_walkable_slope_respects_the_45_degree_limit(void) {
    TEST_ASSERT_TRUE(player_movement_is_walkable_slope(glm_rad(30.0f)));
    TEST_ASSERT_TRUE(player_movement_is_walkable_slope(PLAYER_WALKABLE_SLOPE_LIMIT_RADIANS));
    TEST_ASSERT_FALSE(player_movement_is_walkable_slope(glm_rad(60.0f)));
}

static void test_uphill_component_on_flat_ground_is_zero(void) {
    vec3 up = {0.0f, 1.0f, 0.0f};
    vec3 wish = {1.0f, 0.0f, 0.0f};
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, player_movement_uphill_component(up, wish));
}

static void test_uphill_component_is_positive_moving_upslope_negative_downslope(void) {
    /* Slope rises toward +X (normal tilted away from +X means the
     * ground surface leans so +X is uphill). */
    vec3 tilted = {-sinf(glm_rad(20.0f)), cosf(glm_rad(20.0f)), 0.0f};
    vec3 uphill_wish = {1.0f, 0.0f, 0.0f};
    vec3 downhill_wish = {-1.0f, 0.0f, 0.0f};
    TEST_ASSERT_TRUE(player_movement_uphill_component(tilted, uphill_wish) > 0.0f);
    TEST_ASSERT_TRUE(player_movement_uphill_component(tilted, downhill_wish) < 0.0f);
}

static void test_slope_speed_multiplier_is_one_on_flat_ground(void) {
    TEST_ASSERT_EQUAL_FLOAT(
        1.0f, player_movement_slope_speed_multiplier(PLAYER_SPEED_TIER_RUN, 0.0f, 1.0f));
}

static void test_slope_speed_multiplier_is_zero_past_the_walkable_limit(void) {
    TEST_ASSERT_EQUAL_FLOAT(
        0.0f, player_movement_slope_speed_multiplier(PLAYER_SPEED_TIER_RUN, glm_rad(60.0f), 1.0f));
}

static void test_slope_speed_multiplier_downhill_is_not_penalized(void) {
    /* Downhill (negative uphill component) must clamp to no penalty,
     * not gain a bonus. */
    TEST_ASSERT_EQUAL_FLOAT(1.0f, player_movement_slope_speed_multiplier(
                                       PLAYER_SPEED_TIER_RUN, PLAYER_WALKABLE_SLOPE_LIMIT_RADIANS, -1.0f));
}

static void test_slope_speed_multiplier_max_penalty_at_walkable_limit(void) {
    const float result = player_movement_slope_speed_multiplier(
        PLAYER_SPEED_TIER_SPRINT, PLAYER_WALKABLE_SLOPE_LIMIT_RADIANS, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f - PLAYER_SPRINT_MAX_UPHILL_PENALTY, result);
}

static void test_capsule_dims_match_spec_standing_and_crouching(void) {
    /* Total height = 2 * (half_height + radius). */
    const float standing_height =
        2.0f * (player_movement_capsule_half_height_for_stance(PLAYER_STANCE_STANDING) +
                player_movement_capsule_radius_for_stance(PLAYER_STANCE_STANDING));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.80f, standing_height);

    const float crouching_height =
        2.0f * (player_movement_capsule_half_height_for_stance(PLAYER_STANCE_CROUCHING) +
                player_movement_capsule_radius_for_stance(PLAYER_STANCE_CROUCHING));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.25f, crouching_height);

    /* Radius is the same for both stances - only height changes. */
    TEST_ASSERT_EQUAL_FLOAT(player_movement_capsule_radius_for_stance(PLAYER_STANCE_STANDING),
                             player_movement_capsule_radius_for_stance(PLAYER_STANCE_CROUCHING));
}

static void test_eye_height_offset_matches_spec_above_ground(void) {
    /* Eye height above ground = eye_height_offset + half_height + radius
     * (capsule center's own height above ground). */
    const float standing_above_ground =
        player_movement_eye_height_offset_for_stance(PLAYER_STANCE_STANDING) +
        player_movement_capsule_half_height_for_stance(PLAYER_STANCE_STANDING) +
        player_movement_capsule_radius_for_stance(PLAYER_STANCE_STANDING);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.67f, standing_above_ground);

    const float crouching_above_ground =
        player_movement_eye_height_offset_for_stance(PLAYER_STANCE_CROUCHING) +
        player_movement_capsule_half_height_for_stance(PLAYER_STANCE_CROUCHING) +
        player_movement_capsule_radius_for_stance(PLAYER_STANCE_CROUCHING);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.12f, crouching_above_ground);
}

static void test_crouch_speed_is_slower_than_walk(void) {
    TEST_ASSERT_TRUE(player_movement_speed_for_tier(PLAYER_SPEED_TIER_CROUCH) <
                      player_movement_speed_for_tier(PLAYER_SPEED_TIER_WALK));
}

static void test_approach_scalar_reaches_target_without_overshoot(void) {
    /* rate=2, dt=0.1 -> max_step=0.2, short of the 1.0 gap. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, player_movement_approach_scalar(0.0f, 1.0f, 2.0f, 0.1f));
    /* A large dt must clamp exactly to target, never overshoot. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, player_movement_approach_scalar(0.0f, 1.0f, 2.0f, 10.0f));
    /* Works symmetrically decreasing toward a lower target. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.8f, player_movement_approach_scalar(1.0f, 0.0f, 2.0f, 0.1f));
}

static void test_classify_landing_bands(void) {
    TEST_ASSERT_EQUAL(PLAYER_LANDING_SEVERITY_SOFT, player_movement_classify_landing(0.5f));
    TEST_ASSERT_EQUAL(PLAYER_LANDING_SEVERITY_SOFT,
                       player_movement_classify_landing(PLAYER_LANDING_SOFT_MAX_IMPACT_SPEED_METERS_PER_SEC));
    TEST_ASSERT_EQUAL(PLAYER_LANDING_SEVERITY_STANDARD, player_movement_classify_landing(3.5f));
    TEST_ASSERT_EQUAL(PLAYER_LANDING_SEVERITY_HEAVY, player_movement_classify_landing(6.5f));
    TEST_ASSERT_EQUAL(PLAYER_LANDING_SEVERITY_DAMAGING, player_movement_classify_landing(20.0f));
}

static void test_landing_recovery_seconds_interpolates_by_severity(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, PLAYER_LANDING_RECOVERY_MIN_SECONDS,
                              player_movement_landing_recovery_seconds(PLAYER_LANDING_SEVERITY_SOFT));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, PLAYER_LANDING_RECOVERY_MAX_SECONDS,
                              player_movement_landing_recovery_seconds(PLAYER_LANDING_SEVERITY_DAMAGING));

    const float standard = player_movement_landing_recovery_seconds(PLAYER_LANDING_SEVERITY_STANDARD);
    const float heavy = player_movement_landing_recovery_seconds(PLAYER_LANDING_SEVERITY_HEAVY);
    /* Monotonically increasing with severity. */
    TEST_ASSERT_TRUE(PLAYER_LANDING_RECOVERY_MIN_SECONDS < standard);
    TEST_ASSERT_TRUE(standard < heavy);
    TEST_ASSERT_TRUE(heavy < PLAYER_LANDING_RECOVERY_MAX_SECONDS);
}

/* --- M10: gait / foot-contact stride math (pure) --- */

static void test_gait_stride_initial_state_is_left_and_zero(void) {
    const PlayerGaitStrideState state = player_movement_gait_stride_state_initial();
    TEST_ASSERT_EQUAL(PLAYER_FOOT_LEFT, state.next_foot);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, state.accumulated_meters);
}

static void test_gait_stride_reset_returns_to_left_and_zero(void) {
    PlayerGaitStrideState state = {.next_foot = PLAYER_FOOT_RIGHT, .accumulated_meters = 5.0f};
    player_movement_gait_stride_state_reset(&state);
    TEST_ASSERT_EQUAL(PLAYER_FOOT_LEFT, state.next_foot);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, state.accumulated_meters);
}

static void test_gait_stride_no_emission_below_threshold(void) {
    PlayerGaitStrideState state = player_movement_gait_stride_state_initial();
    PlayerFoot             feet[4];
    uint32_t               dropped = 99;
    const uint32_t         emitted =
        player_movement_gait_stride_advance(&state, 0.3f, 1.0f, 2u, feet, 4u, &dropped);
    TEST_ASSERT_EQUAL(0u, emitted);
    TEST_ASSERT_EQUAL(0u, dropped);
    TEST_ASSERT_EQUAL_FLOAT(0.3f, state.accumulated_meters);
}

static void test_gait_stride_alternates_left_right_across_calls(void) {
    PlayerGaitStrideState state = player_movement_gait_stride_state_initial();
    PlayerFoot             feet[4];
    uint32_t               dropped;

    for (int i = 0; i < 5; ++i) {
        const uint32_t emitted =
            player_movement_gait_stride_advance(&state, 1.0f, 1.0f, 2u, feet, 4u, &dropped);
        TEST_ASSERT_EQUAL(1u, emitted);
        TEST_ASSERT_EQUAL(0u, dropped);
        TEST_ASSERT_EQUAL((i % 2 == 0) ? PLAYER_FOOT_LEFT : PLAYER_FOOT_RIGHT, feet[0]);
    }
    /* Every complete stride's distance is consumed - nothing left over
     * after exactly 1.0m against a 1.0m stride length each call. */
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, state.accumulated_meters);
}

static void test_gait_stride_cap_drops_additional_whole_strides(void) {
    PlayerGaitStrideState state = player_movement_gait_stride_state_initial();
    PlayerFoot             feet[8];
    uint32_t               dropped = 0;

    /* 5.5 strides worth of displacement in one call, capped at 2. */
    const uint32_t emitted =
        player_movement_gait_stride_advance(&state, 5.5f, 1.0f, 2u, feet, 8u, &dropped);

    TEST_ASSERT_EQUAL(2u, emitted);
    TEST_ASSERT_EQUAL(3u, dropped); /* 5 whole strides total - 2 emitted = 3 dropped */
    TEST_ASSERT_EQUAL(PLAYER_FOOT_LEFT, feet[0]);
    TEST_ASSERT_EQUAL(PLAYER_FOOT_RIGHT, feet[1]);
    /* Only the fractional remainder below one stride survives. */
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, state.accumulated_meters);
    /* Phase still alternates through the dropped strides too (5 total),
     * so the NEXT call continues correctly from foot #6 = LEFT again
     * (5 toggles from LEFT lands on RIGHT as next_foot after an odd
     * count - verified indirectly via the next call below). */
    dropped = 0;
    const uint32_t next_emitted =
        player_movement_gait_stride_advance(&state, 0.5f, 1.0f, 2u, feet, 8u, &dropped);
    TEST_ASSERT_EQUAL(1u, next_emitted);
    TEST_ASSERT_EQUAL(0u, dropped);
    TEST_ASSERT_EQUAL(PLAYER_FOOT_RIGHT, feet[0]);
}

static void test_gait_stride_cap_does_not_cause_later_burst(void) {
    PlayerGaitStrideState state = player_movement_gait_stride_state_initial();
    PlayerFoot             feet[8];
    uint32_t               dropped = 0;

    const uint32_t emitted =
        player_movement_gait_stride_advance(&state, 5.5f, 1.0f, 2u, feet, 8u, &dropped);
    TEST_ASSERT_EQUAL(2u, emitted);
    TEST_ASSERT_TRUE(dropped > 0);

    /* No further realized movement (player stopped) - the dropped
     * strides' distance must never resurface as a delayed burst. */
    const uint32_t stationary_emitted =
        player_movement_gait_stride_advance(&state, 0.0f, 1.0f, 2u, feet, 8u, &dropped);
    TEST_ASSERT_EQUAL(0u, stationary_emitted);
    TEST_ASSERT_EQUAL(0u, dropped);
}

static void test_gait_stride_length_and_intensity_are_data_driven_per_gait(void) {
    const PlayerGait walk = {.stance = PLAYER_STANCE_STANDING, .speed_tier = PLAYER_SPEED_TIER_WALK};
    const PlayerGait run = {.stance = PLAYER_STANCE_STANDING, .speed_tier = PLAYER_SPEED_TIER_RUN};
    const PlayerGait sprint = {.stance = PLAYER_STANCE_STANDING, .speed_tier = PLAYER_SPEED_TIER_SPRINT};
    const PlayerGait crouch = {.stance = PLAYER_STANCE_CROUCHING, .speed_tier = PLAYER_SPEED_TIER_CROUCH};

    TEST_ASSERT_EQUAL_FLOAT(PLAYER_STRIDE_LENGTH_WALK_METERS, player_movement_stride_length_for_gait(walk));
    TEST_ASSERT_EQUAL_FLOAT(PLAYER_STRIDE_LENGTH_RUN_METERS, player_movement_stride_length_for_gait(run));
    TEST_ASSERT_EQUAL_FLOAT(PLAYER_STRIDE_LENGTH_SPRINT_METERS,
                              player_movement_stride_length_for_gait(sprint));
    TEST_ASSERT_EQUAL_FLOAT(PLAYER_STRIDE_LENGTH_CROUCH_METERS,
                              player_movement_stride_length_for_gait(crouch));

    TEST_ASSERT_EQUAL_FLOAT(PLAYER_FOOTSTEP_INTENSITY_WALK, player_movement_footstep_intensity_for_gait(walk));
    TEST_ASSERT_EQUAL_FLOAT(PLAYER_FOOTSTEP_INTENSITY_SPRINT,
                              player_movement_footstep_intensity_for_gait(sprint));
    TEST_ASSERT_TRUE(player_movement_footstep_intensity_for_gait(crouch) <
                      player_movement_footstep_intensity_for_gait(sprint));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_stamina_drains_while_sprinting_and_locks_out_at_zero);
    RUN_TEST(test_locked_out_sprint_stays_off_below_reenable_threshold);
    RUN_TEST(test_stamina_regenerates_and_clears_lockout_exactly_at_threshold);
    RUN_TEST(test_holding_sprint_while_standing_still_costs_nothing);
    RUN_TEST(test_sprint_context_disallowed_falls_back_without_draining);
    RUN_TEST(test_select_speed_tier_defaults_to_run);
    RUN_TEST(test_select_speed_tier_walk_modifier_overrides_default);
    RUN_TEST(test_select_speed_tier_sprint_wins_over_walk_modifier);
    RUN_TEST(test_speed_accel_decel_for_tier_are_distinct_per_gait);
    RUN_TEST(test_stopping_distances_land_within_spec_target_ranges);
    RUN_TEST(test_approach_velocity_reaches_target_without_overshoot);
    RUN_TEST(test_approach_velocity_decelerates_to_zero);
    RUN_TEST(test_wrap_angle_radians_stays_in_range);
    RUN_TEST(test_angle_difference_takes_the_short_way_around);
    RUN_TEST(test_yaw_from_direction_matches_camera_convention);
    RUN_TEST(test_resolve_body_yaw_follows_movement_direction_while_moving);
    RUN_TEST(test_resolve_body_yaw_holds_while_stationary_within_threshold);
    RUN_TEST(test_resolve_body_yaw_turns_in_place_beyond_threshold);
    RUN_TEST(test_is_within_cone_accepts_and_rejects_correctly);
    RUN_TEST(test_classify_turn_from_a_stop_is_always_minor);
    RUN_TEST(test_classify_turn_severity_by_angle);
    RUN_TEST(test_turn_speed_multiplier_decreases_with_severity);
    RUN_TEST(test_clamp_sprint_turn_passes_through_from_a_stop);
    RUN_TEST(test_clamp_sprint_turn_limits_a_sharp_redirect_at_full_speed);
    RUN_TEST(test_slope_angle_radians_zero_for_flat_ground);
    RUN_TEST(test_slope_angle_radians_matches_a_known_tilt);
    RUN_TEST(test_is_walkable_slope_respects_the_45_degree_limit);
    RUN_TEST(test_uphill_component_on_flat_ground_is_zero);
    RUN_TEST(test_uphill_component_is_positive_moving_upslope_negative_downslope);
    RUN_TEST(test_slope_speed_multiplier_is_one_on_flat_ground);
    RUN_TEST(test_slope_speed_multiplier_is_zero_past_the_walkable_limit);
    RUN_TEST(test_slope_speed_multiplier_downhill_is_not_penalized);
    RUN_TEST(test_slope_speed_multiplier_max_penalty_at_walkable_limit);
    RUN_TEST(test_capsule_dims_match_spec_standing_and_crouching);
    RUN_TEST(test_eye_height_offset_matches_spec_above_ground);
    RUN_TEST(test_crouch_speed_is_slower_than_walk);
    RUN_TEST(test_approach_scalar_reaches_target_without_overshoot);
    RUN_TEST(test_classify_landing_bands);
    RUN_TEST(test_landing_recovery_seconds_interpolates_by_severity);

    RUN_TEST(test_gait_stride_initial_state_is_left_and_zero);
    RUN_TEST(test_gait_stride_reset_returns_to_left_and_zero);
    RUN_TEST(test_gait_stride_no_emission_below_threshold);
    RUN_TEST(test_gait_stride_alternates_left_right_across_calls);
    RUN_TEST(test_gait_stride_cap_drops_additional_whole_strides);
    RUN_TEST(test_gait_stride_cap_does_not_cause_later_burst);
    RUN_TEST(test_gait_stride_length_and_intensity_are_data_driven_per_gait);

    return UNITY_END();
}
