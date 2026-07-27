/*
 * camera_motion is pure math/logic (no Camera, Physics, ECS, or Input
 * dependency beyond player_movement.h's plain structs/enums) - these
 * tests exercise it directly with no engine running at all, driving a
 * synthetic PlayerGaitStrideState through player_movement.c's own
 * public functions exactly the way infantry_entity.c does, never a
 * second odometer of the test's own invention.
 */
#include "camera_motion.h"

#include "camera_motion_tuning.h"
#include "player_tuning.h"
#include "sim_clock.h"

#include "unity.h"

#include <math.h>

void setUp(void) {
}

void tearDown(void) {
}

static const PlayerGait WALK_GAIT = {.stance = PLAYER_STANCE_STANDING, .speed_tier = PLAYER_SPEED_TIER_WALK};
static const PlayerGait RUN_GAIT = {.stance = PLAYER_STANCE_STANDING, .speed_tier = PLAYER_SPEED_TIER_RUN};
static const PlayerGait SPRINT_GAIT = {.stance = PLAYER_STANCE_STANDING, .speed_tier = PLAYER_SPEED_TIER_SPRINT};
static const PlayerGait CROUCH_GAIT = {.stance = PLAYER_STANCE_CROUCHING, .speed_tier = PLAYER_SPEED_TIER_CROUCH};

static PlayerGaitPresentationSnapshot make_snapshot(PlayerGait gait, float progress01, bool motion_valid,
                                                      uint32_t reset_generation) {
    return (PlayerGaitPresentationSnapshot){
        .foot_phase = PLAYER_FOOT_LEFT,
        .stride_progress01 = progress01,
        .stride_length_meters = 1.0f,
        .gait = gait,
        .motion_valid = motion_valid,
        .reset_generation = reset_generation,
    };
}

/* --- pure gait bob curve --- */

static void test_gait_vertical_offset_zero_at_footfalls_peak_mid_stride(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, camera_motion_gait_vertical_offset(0.0f, 0.02f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, camera_motion_gait_vertical_offset(1.0f, 0.02f));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.02f, camera_motion_gait_vertical_offset(0.5f, 0.02f));
}

static void test_gait_lateral_offset_sign_matches_foot_phase(void) {
    const float left_extremum = camera_motion_gait_lateral_offset(PLAYER_FOOT_LEFT, 0.5f, 0.01f);
    const float right_extremum = camera_motion_gait_lateral_offset(PLAYER_FOOT_RIGHT, 0.5f, 0.01f);
    TEST_ASSERT_TRUE(left_extremum < 0.0f);
    TEST_ASSERT_TRUE(right_extremum > 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, fabsf(left_extremum), fabsf(right_extremum));
}

/* --- gating: idle / airborne / wall-blocked (all surfaced as
 * motion_valid == false at this layer) --- */

static void test_no_gait_motion_while_motion_invalid(void) {
    CameraMotionState state;
    camera_motion_state_init(&state);
    CameraMotionConfig config = camera_motion_config_default();

    vec3 velocity = {0.0f, 0.0f, 0.0f};
    PlayerGaitPresentationSnapshot snapshot = make_snapshot(RUN_GAIT, 0.5f, /*motion_valid=*/false, 1);
    camera_motion_update_fixed(&state, &config, snapshot, velocity, 0.0f, nullptr, SIM_FIXED_DELTA_SECONDS);

    TEST_ASSERT_EQUAL_FLOAT(0.0f, state.current_offset[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, state.current_offset[1]);
}

static void test_no_continued_bob_when_blocked(void) {
    /* Same mechanism as idle/airborne at this layer - a wall-blocked
     * tick's realized displacement is ~0, which infantry_entity.c
     * surfaces as motion_valid == false exactly like idle. */
    CameraMotionState state;
    camera_motion_state_init(&state);
    CameraMotionConfig config = camera_motion_config_default();
    vec3               velocity = {0.0f, 0.0f, 0.0f};

    /* Bob was active and had ramped up amplitude for a while... */
    for (int i = 0; i < 120; ++i) {
        PlayerGaitPresentationSnapshot snapshot = make_snapshot(RUN_GAIT, 0.5f, true, 1);
        camera_motion_update_fixed(&state, &config, snapshot, velocity, 0.0f, nullptr, SIM_FIXED_DELTA_SECONDS);
    }
    TEST_ASSERT_TRUE(fabsf(state.current_offset[1]) > 0.0001f);

    /* ...then blocked (motion_valid false) for long enough to settle. */
    for (int i = 0; i < 120; ++i) {
        PlayerGaitPresentationSnapshot snapshot = make_snapshot(RUN_GAIT, 0.5f, false, 1);
        camera_motion_update_fixed(&state, &config, snapshot, velocity, 0.0f, nullptr, SIM_FIXED_DELTA_SECONDS);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, state.current_offset[1]);
}

/* --- relative amplitude ordering --- */

static float settle_and_get_vertical(PlayerGait gait) {
    CameraMotionState state;
    camera_motion_state_init(&state);
    CameraMotionConfig config = camera_motion_config_default();
    vec3               velocity = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 300; ++i) {
        PlayerGaitPresentationSnapshot snapshot = make_snapshot(gait, 0.5f, true, 1);
        camera_motion_update_fixed(&state, &config, snapshot, velocity, 0.0f, nullptr, SIM_FIXED_DELTA_SECONDS);
    }
    return fabsf(state.current_offset[1]);
}

static void test_amplitude_ordering_crouch_walk_run_sprint(void) {
    const float crouch = settle_and_get_vertical(CROUCH_GAIT);
    const float walk = settle_and_get_vertical(WALK_GAIT);
    const float run = settle_and_get_vertical(RUN_GAIT);
    const float sprint = settle_and_get_vertical(SPRINT_GAIT);

    TEST_ASSERT_TRUE(crouch < walk);
    TEST_ASSERT_TRUE(walk < run);
    TEST_ASSERT_TRUE(run < sprint);
}

/* --- continuous phase across gait changes (no snap) --- */

static void test_gait_change_does_not_snap_offset(void) {
    CameraMotionState state;
    camera_motion_state_init(&state);
    CameraMotionConfig config = camera_motion_config_default();
    vec3               velocity = {0.0f, 0.0f, 0.0f};

    for (int i = 0; i < 300; ++i) {
        PlayerGaitPresentationSnapshot snapshot = make_snapshot(WALK_GAIT, 0.5f, true, 1);
        camera_motion_update_fixed(&state, &config, snapshot, velocity, 0.0f, nullptr, SIM_FIXED_DELTA_SECONDS);
    }
    const float before_switch = state.current_offset[1];

    /* Switch to Sprint (much larger target amplitude) for one tick. */
    PlayerGaitPresentationSnapshot switched = make_snapshot(SPRINT_GAIT, 0.5f, true, 1);
    camera_motion_update_fixed(&state, &config, switched, velocity, 0.0f, nullptr, SIM_FIXED_DELTA_SECONDS);
    const float after_one_tick = state.current_offset[1];

    /* One tick can't have reached Sprint's full amplitude - the jump
     * this single tick produced must be far smaller than the total gap
     * between Walk's and Sprint's settled amplitudes. */
    const float full_sprint = settle_and_get_vertical(SPRINT_GAIT);
    const float one_tick_jump = fabsf(after_one_tick - before_switch);
    const float total_gap = fabsf(full_sprint - before_switch);
    TEST_ASSERT_TRUE(one_tick_jump < total_gap * 0.5f);
}

/* --- smooth start/stop --- */

static void test_smooth_start_ramps_up_not_instant(void) {
    CameraMotionState state;
    camera_motion_state_init(&state);
    CameraMotionConfig config = camera_motion_config_default();
    vec3               velocity = {0.0f, 0.0f, 0.0f};

    PlayerGaitPresentationSnapshot snapshot = make_snapshot(RUN_GAIT, 0.5f, true, 1);
    camera_motion_update_fixed(&state, &config, snapshot, velocity, 0.0f, nullptr, SIM_FIXED_DELTA_SECONDS);
    const float first_tick_offset = fabsf(state.current_offset[1]);

    const float full_amplitude = settle_and_get_vertical(RUN_GAIT);
    TEST_ASSERT_TRUE(first_tick_offset < full_amplitude * 0.5f);
}

static void test_smooth_stop_fades_out_not_instant(void) {
    CameraMotionState state;
    camera_motion_state_init(&state);
    CameraMotionConfig config = camera_motion_config_default();
    vec3               velocity = {0.0f, 0.0f, 0.0f};

    for (int i = 0; i < 300; ++i) {
        PlayerGaitPresentationSnapshot snapshot = make_snapshot(RUN_GAIT, 0.5f, true, 1);
        camera_motion_update_fixed(&state, &config, snapshot, velocity, 0.0f, nullptr, SIM_FIXED_DELTA_SECONDS);
    }
    const float settled = fabsf(state.current_offset[1]);

    PlayerGaitPresentationSnapshot stopped = make_snapshot(RUN_GAIT, 0.5f, false, 1);
    camera_motion_update_fixed(&state, &config, stopped, velocity, 0.0f, nullptr, SIM_FIXED_DELTA_SECONDS);
    const float first_tick_after_stop = fabsf(state.current_offset[1]);

    TEST_ASSERT_TRUE(first_tick_after_stop > settled * 0.5f); /* still mostly there one tick later */
    TEST_ASSERT_TRUE(first_tick_after_stop < settled);        /* but already fading */
}

/* --- landing --- */

static InfantryFallLandingEvent make_landing(PlayerLandingSeverity severity) {
    return (InfantryFallLandingEvent){
        .fall_start_height = 5.0f,
        .landing_height = 0.0f,
        .max_downward_speed = 10.0f,
        .impact_speed = 10.0f,
        .air_time_seconds = 1.0f,
        .surface_normal = {0.0f, 1.0f, 0.0f},
        .was_jump_initiated = false,
        .severity = severity,
    };
}

static float trigger_and_get_peak(PlayerLandingSeverity severity) {
    CameraMotionState state;
    camera_motion_state_init(&state);
    CameraMotionConfig config = camera_motion_config_default();
    vec3               velocity = {0.0f, 0.0f, 0.0f};

    PlayerGaitPresentationSnapshot snapshot = make_snapshot(RUN_GAIT, 0.0f, false, 1);
    InfantryFallLandingEvent       landing = make_landing(severity);
    camera_motion_update_fixed(&state, &config, snapshot, velocity, 0.0f, &landing, SIM_FIXED_DELTA_SECONDS);
    return state.landing_peak_amplitude_meters;
}

static void test_landing_amplitude_scales_with_severity(void) {
    const float soft = trigger_and_get_peak(PLAYER_LANDING_SEVERITY_SOFT);
    const float standard = trigger_and_get_peak(PLAYER_LANDING_SEVERITY_STANDARD);
    const float heavy = trigger_and_get_peak(PLAYER_LANDING_SEVERITY_HEAVY);
    const float damaging = trigger_and_get_peak(PLAYER_LANDING_SEVERITY_DAMAGING);

    TEST_ASSERT_TRUE(soft < standard);
    TEST_ASSERT_TRUE(standard < heavy);
    TEST_ASSERT_TRUE(heavy < damaging);
}

static void test_landing_response_bounded_at_extreme_severity(void) {
    const float damaging = trigger_and_get_peak(PLAYER_LANDING_SEVERITY_DAMAGING);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, CAMERA_MOTION_LANDING_DAMAGING_AMPLITUDE_METERS, damaging);
}

static void test_landing_never_produces_rotation(void) {
    /* Structural guarantee, asserted directly: CameraMotionState has no
     * rotation field at all, and the offset this module produces is a
     * position-only vec3 - there is nothing here that COULD express a
     * rotational shake. */
    CameraMotionState state;
    camera_motion_state_init(&state);
    CameraMotionConfig       config = camera_motion_config_default();
    vec3                     velocity = {0.0f, 0.0f, 0.0f};
    InfantryFallLandingEvent landing = make_landing(PLAYER_LANDING_SEVERITY_DAMAGING);
    PlayerGaitPresentationSnapshot snapshot = make_snapshot(RUN_GAIT, 0.0f, false, 1);
    camera_motion_update_fixed(&state, &config, snapshot, velocity, 0.0f, &landing, SIM_FIXED_DELTA_SECONDS);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, state.current_offset[0]); /* no incidental lateral kick from landing */
}

static void test_second_landing_mid_recovery_blends_not_stacks(void) {
    CameraMotionState state;
    camera_motion_state_init(&state);
    CameraMotionConfig config = camera_motion_config_default();
    vec3               velocity = {0.0f, 0.0f, 0.0f};
    PlayerGaitPresentationSnapshot snapshot = make_snapshot(RUN_GAIT, 0.0f, false, 1);

    InfantryFallLandingEvent first_landing = make_landing(PLAYER_LANDING_SEVERITY_HEAVY);
    camera_motion_update_fixed(&state, &config, snapshot, velocity, 0.0f, &first_landing,
                                SIM_FIXED_DELTA_SECONDS);
    const float peak_after_first = state.landing_peak_amplitude_meters;

    /* A second HEAVY landing arrives one tick later, mid-recovery. */
    InfantryFallLandingEvent second_landing = make_landing(PLAYER_LANDING_SEVERITY_HEAVY);
    camera_motion_update_fixed(&state, &config, snapshot, velocity, 0.0f, &second_landing,
                                SIM_FIXED_DELTA_SECONDS);
    const float peak_after_second = state.landing_peak_amplitude_meters;

    /* Blended (max), not summed - two HEAVY landings never add up to
     * roughly double a single HEAVY's amplitude. */
    TEST_ASSERT_FLOAT_WITHIN(0.001f, peak_after_first, peak_after_second);
    TEST_ASSERT_TRUE(peak_after_second < peak_after_first * 1.9f);
}

/* --- accessibility --- */

static void test_zero_intensity_produces_exactly_zero_offset(void) {
    CameraMotionState  state;
    camera_motion_state_init(&state);
    CameraMotionConfig config = {
        .global_intensity = 0.0f, .gait_intensity = 0.0f, .landing_intensity = 0.0f, .inertia_intensity = 0.0f};

    vec3                      velocity = {5.0f, 0.0f, 0.0f}; /* would otherwise induce inertia */
    InfantryFallLandingEvent  landing = make_landing(PLAYER_LANDING_SEVERITY_DAMAGING);
    for (int i = 0; i < 30; ++i) {
        PlayerGaitPresentationSnapshot snapshot = make_snapshot(SPRINT_GAIT, 0.5f, true, 1);
        camera_motion_update_fixed(&state, &config, snapshot, velocity, 0.0f, i == 0 ? &landing : nullptr,
                                    SIM_FIXED_DELTA_SECONDS);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, state.current_offset[0]);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, state.current_offset[1]);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, state.current_offset[2]);
    }
}

/* --- discontinuity handling --- */

static void test_reset_generation_change_snaps_instead_of_interpolating(void) {
    CameraMotionState state;
    camera_motion_state_init(&state);
    CameraMotionConfig config = camera_motion_config_default();
    vec3               velocity = {0.0f, 0.0f, 0.0f};

    for (int i = 0; i < 300; ++i) {
        PlayerGaitPresentationSnapshot snapshot = make_snapshot(SPRINT_GAIT, 0.5f, true, 1);
        camera_motion_update_fixed(&state, &config, snapshot, velocity, 0.0f, nullptr, SIM_FIXED_DELTA_SECONDS);
    }
    TEST_ASSERT_TRUE(fabsf(state.current_offset[1]) > 0.0001f);

    /* Discontinuity: reset_generation changes (teleport/noclip/spawn). */
    PlayerGaitPresentationSnapshot after_reset = make_snapshot(SPRINT_GAIT, 0.5f, true, 2);
    camera_motion_update_fixed(&state, &config, after_reset, velocity, 0.0f, nullptr, SIM_FIXED_DELTA_SECONDS);

    /* previous_offset must be zero (not the old settled value) - an
     * interpolation query right after this tick must not blend across
     * the discontinuity. */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, state.previous_offset[1]);
}

/* --- render-rate interpolation --- */

static void test_render_offset_interpolates_between_ticks(void) {
    CameraMotionState state;
    camera_motion_state_init(&state);
    CameraMotionConfig config = camera_motion_config_default();
    vec3               velocity = {0.0f, 0.0f, 0.0f};

    PlayerGaitPresentationSnapshot snapshot_a = make_snapshot(RUN_GAIT, 0.0f, true, 1);
    camera_motion_update_fixed(&state, &config, snapshot_a, velocity, 0.0f, nullptr, SIM_FIXED_DELTA_SECONDS);
    vec3 offset_a;
    glm_vec3_copy(state.current_offset, offset_a);

    PlayerGaitPresentationSnapshot snapshot_b = make_snapshot(RUN_GAIT, 0.5f, true, 1);
    camera_motion_update_fixed(&state, &config, snapshot_b, velocity, 0.0f, nullptr, SIM_FIXED_DELTA_SECONDS);
    vec3 offset_b;
    glm_vec3_copy(state.current_offset, offset_b);

    vec3 at_start, at_mid, at_end;
    camera_motion_get_render_offset(&state, 0.0f, at_start);
    camera_motion_get_render_offset(&state, 0.5f, at_mid);
    camera_motion_get_render_offset(&state, 1.0f, at_end);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, offset_a[1], at_start[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, offset_b[1], at_end[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, (offset_a[1] + offset_b[1]) * 0.5f, at_mid[1]);
}

/* --- frame-rate equivalence: real render frames, zero/one/multiple
 * fixed ticks each, compared at equivalent SIMULATION time across
 * 30/60/120/144/240 FPS --- */

static float sample_vertical_offset_at_time(float fps, float target_seconds) {
    SimClock clock;
    sim_clock_init(&clock);
    CameraMotionState state;
    camera_motion_state_init(&state);
    CameraMotionConfig config = camera_motion_config_default();

    PlayerGaitStrideState stride = player_movement_gait_stride_state_initial();
    const float           constant_speed_meters_per_sec = 3.0f;
    const float           stride_length = player_movement_stride_length_for_gait(RUN_GAIT);
    const float           render_delta_seconds = 1.0f / fps;

    float last_vertical = 0.0f;
    float elapsed = 0.0f;
    while (elapsed < target_seconds) {
        sim_clock_accumulate(&clock, render_delta_seconds);
        while (sim_clock_consume_step(&clock)) {
            const float displacement = constant_speed_meters_per_sec * SIM_FIXED_DELTA_SECONDS;
            PlayerFoot  feet[2];
            uint32_t    dropped;
            player_movement_gait_stride_advance(&stride, displacement, stride_length, 2, feet, 2, &dropped);
            PlayerGaitPresentationSnapshot snapshot = {
                .foot_phase = stride.next_foot,
                .stride_progress01 = player_movement_gait_stride_progress01(stride, stride_length),
                .stride_length_meters = stride_length,
                .gait = RUN_GAIT,
                .motion_valid = true,
                .reset_generation = 1,
            };
            vec3 velocity = {constant_speed_meters_per_sec, 0.0f, 0.0f};
            camera_motion_update_fixed(&state, &config, snapshot, velocity, 0.0f, nullptr,
                                        SIM_FIXED_DELTA_SECONDS);
        }
        const float alpha = clock.accumulator_seconds / SIM_FIXED_DELTA_SECONDS;
        vec3        offset;
        camera_motion_get_render_offset(&state, alpha, offset);
        last_vertical = offset[1];
        elapsed += render_delta_seconds;
    }
    return last_vertical;
}

static void test_frame_rate_equivalence_30_60_120_144_240(void) {
    const float target_seconds = 2.0f; /* past amplitude ramp-up and several bob cycles */
    const float at_30 = sample_vertical_offset_at_time(30.0f, target_seconds);
    const float at_60 = sample_vertical_offset_at_time(60.0f, target_seconds);
    const float at_120 = sample_vertical_offset_at_time(120.0f, target_seconds);
    const float at_144 = sample_vertical_offset_at_time(144.0f, target_seconds);
    const float at_240 = sample_vertical_offset_at_time(240.0f, target_seconds);

    /* Amplitude at this gait/tick rate tops out around
     * CAMERA_MOTION_RUN_VERTICAL_AMPLITUDE_METERS (~1.6cm) - a few mm
     * of tolerance across frame rates is well under 10% of that. */
    const float tolerance = 0.002f;
    TEST_ASSERT_FLOAT_WITHIN(tolerance, at_60, at_30);
    TEST_ASSERT_FLOAT_WITHIN(tolerance, at_60, at_120);
    TEST_ASSERT_FLOAT_WITHIN(tolerance, at_60, at_144);
    TEST_ASSERT_FLOAT_WITHIN(tolerance, at_60, at_240);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_gait_vertical_offset_zero_at_footfalls_peak_mid_stride);
    RUN_TEST(test_gait_lateral_offset_sign_matches_foot_phase);
    RUN_TEST(test_no_gait_motion_while_motion_invalid);
    RUN_TEST(test_no_continued_bob_when_blocked);
    RUN_TEST(test_amplitude_ordering_crouch_walk_run_sprint);
    RUN_TEST(test_gait_change_does_not_snap_offset);
    RUN_TEST(test_smooth_start_ramps_up_not_instant);
    RUN_TEST(test_smooth_stop_fades_out_not_instant);
    RUN_TEST(test_landing_amplitude_scales_with_severity);
    RUN_TEST(test_landing_response_bounded_at_extreme_severity);
    RUN_TEST(test_landing_never_produces_rotation);
    RUN_TEST(test_second_landing_mid_recovery_blends_not_stacks);
    RUN_TEST(test_zero_intensity_produces_exactly_zero_offset);
    RUN_TEST(test_reset_generation_change_snaps_instead_of_interpolating);
    RUN_TEST(test_render_offset_interpolates_between_ticks);
    RUN_TEST(test_frame_rate_equivalence_30_60_120_144_240);

    return UNITY_END();
}
