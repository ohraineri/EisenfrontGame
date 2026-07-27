/*
 * See camera_motion.h's file header comment for the overall contract.
 *
 * Gait bob frequency: stride_progress01 covers ONE single-foot stride
 * (0 right after a contact, 1 at the next one), not a full left+right
 * gait cycle. Using sinf(progress * PI) for vertical bob therefore
 * already produces two peaks per full L+R cycle (one per footfall) -
 * the physically-correct double-bump vertical bob real bipedal gait
 * has, without any extra frequency math. Lateral bob uses the same
 * curve but flips sign by which foot is currently swinging (LEFT
 * pending = lean one way, RIGHT pending = the other), so it completes
 * one full left-right cycle per TWO footfalls, matching a real stride's
 * side-to-side sway.
 */
#include "camera_motion.h"

#include "camera_motion_tuning.h"

#include <math.h>

CameraMotionConfig camera_motion_config_default(void) {
    return (CameraMotionConfig){
        .global_intensity = 1.0f,
        .gait_intensity = 1.0f,
        .landing_intensity = 1.0f,
        .inertia_intensity = 1.0f,
    };
}

static float clamp01(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

float camera_motion_gait_vertical_offset(float stride_progress01, float amplitude_meters) {
    return amplitude_meters * sinf(clamp01(stride_progress01) * GLM_PIf);
}

float camera_motion_gait_lateral_offset(PlayerFoot foot_phase, float stride_progress01, float amplitude_meters) {
    const float side = (foot_phase == PLAYER_FOOT_LEFT) ? -1.0f : 1.0f;
    return side * amplitude_meters * sinf(clamp01(stride_progress01) * GLM_PIf);
}

static void gait_amplitude_targets_for_tier(PlayerSpeedTier tier, float *out_vertical, float *out_lateral) {
    switch (tier) {
        case PLAYER_SPEED_TIER_WALK:
            *out_vertical = CAMERA_MOTION_WALK_VERTICAL_AMPLITUDE_METERS;
            *out_lateral = CAMERA_MOTION_WALK_LATERAL_AMPLITUDE_METERS;
            return;
        case PLAYER_SPEED_TIER_SPRINT:
            *out_vertical = CAMERA_MOTION_SPRINT_VERTICAL_AMPLITUDE_METERS;
            *out_lateral = CAMERA_MOTION_SPRINT_LATERAL_AMPLITUDE_METERS;
            return;
        case PLAYER_SPEED_TIER_CROUCH:
            *out_vertical = CAMERA_MOTION_CROUCH_VERTICAL_AMPLITUDE_METERS;
            *out_lateral = CAMERA_MOTION_CROUCH_LATERAL_AMPLITUDE_METERS;
            return;
        case PLAYER_SPEED_TIER_RUN:
        case PLAYER_SPEED_TIER_COUNT:
        default:
            *out_vertical = CAMERA_MOTION_RUN_VERTICAL_AMPLITUDE_METERS;
            *out_lateral = CAMERA_MOTION_RUN_LATERAL_AMPLITUDE_METERS;
            return;
    }
}

static float landing_amplitude_for_severity(PlayerLandingSeverity severity) {
    switch (severity) {
        case PLAYER_LANDING_SEVERITY_SOFT:
            return CAMERA_MOTION_LANDING_SOFT_AMPLITUDE_METERS;
        case PLAYER_LANDING_SEVERITY_STANDARD:
            return CAMERA_MOTION_LANDING_STANDARD_AMPLITUDE_METERS;
        case PLAYER_LANDING_SEVERITY_HEAVY:
            return CAMERA_MOTION_LANDING_HEAVY_AMPLITUDE_METERS;
        case PLAYER_LANDING_SEVERITY_DAMAGING:
        default:
            /* Ceiling: even a more extreme impact than DAMAGING's own
             * band (classify_landing() has no tier above it) never
             * exceeds this. */
            return CAMERA_MOTION_LANDING_DAMAGING_AMPLITUDE_METERS;
    }
}

void camera_motion_state_init(CameraMotionState *state) {
    *state = (CameraMotionState){0};
}

void camera_motion_reset(CameraMotionState *state) {
    glm_vec3_zero(state->previous_offset);
    glm_vec3_zero(state->current_offset);
    state->gait_active_blend = 0.0f;
    state->gait_vertical_amplitude_meters = 0.0f;
    state->gait_lateral_amplitude_meters = 0.0f;
    glm_vec3_zero(state->inertia_offset);
    glm_vec3_zero(state->previous_horizontal_velocity);
    glm_vec3_zero(state->landing_offset);
    state->landing_peak_amplitude_meters = 0.0f;
    state->landing_recovery_seconds_remaining = 0.0f;
    state->landing_recovery_total_seconds = 0.0f;
    /* last_seen_gait_reset_generation deliberately untouched - see
     * camera_motion_update_fixed()'s own bookkeeping for why. */
}

void camera_motion_update_fixed(CameraMotionState *state, const CameraMotionConfig *config,
                                 PlayerGaitPresentationSnapshot snapshot, vec3 horizontal_velocity,
                                 float body_yaw_radians, const InfantryFallLandingEvent *landing_event_or_null,
                                 float fixed_delta_seconds) {
    if (snapshot.reset_generation != state->last_seen_gait_reset_generation) {
        camera_motion_reset(state);
        state->last_seen_gait_reset_generation = snapshot.reset_generation;
    }

    glm_vec3_copy(state->current_offset, state->previous_offset);

    /* --- gait bob --- */
    float target_vertical_amplitude, target_lateral_amplitude;
    gait_amplitude_targets_for_tier(snapshot.gait.speed_tier, &target_vertical_amplitude,
                                     &target_lateral_amplitude);
    state->gait_vertical_amplitude_meters = player_movement_approach_scalar(
        state->gait_vertical_amplitude_meters, target_vertical_amplitude,
        CAMERA_MOTION_GAIT_AMPLITUDE_BLEND_RATE_PER_SEC, fixed_delta_seconds);
    state->gait_lateral_amplitude_meters = player_movement_approach_scalar(
        state->gait_lateral_amplitude_meters, target_lateral_amplitude,
        CAMERA_MOTION_GAIT_AMPLITUDE_BLEND_RATE_PER_SEC, fixed_delta_seconds);
    state->gait_active_blend =
        player_movement_approach_scalar(state->gait_active_blend, snapshot.motion_valid ? 1.0f : 0.0f,
                                         CAMERA_MOTION_GAIT_ACTIVE_BLEND_RATE_PER_SEC, fixed_delta_seconds);

    vec3 gait_offset = {
        camera_motion_gait_lateral_offset(snapshot.foot_phase, snapshot.stride_progress01,
                                            state->gait_lateral_amplitude_meters) *
            state->gait_active_blend,
        camera_motion_gait_vertical_offset(snapshot.stride_progress01, state->gait_vertical_amplitude_meters) *
            state->gait_active_blend,
        0.0f,
    };
    glm_vec3_scale(gait_offset, config->gait_intensity, gait_offset);

    /* --- start/stop/turn inertia: realized world-space acceleration,
     * converted into the body's local eye-space frame (x=right,
     * z=forward - same convention camera_get_right()/camera_get_forward()
     * use: forward=(cos(yaw),*,sin(yaw)), right=(-sin(yaw),*,cos(yaw))). */
    vec3 world_accel;
    glm_vec3_sub(horizontal_velocity, state->previous_horizontal_velocity, world_accel);
    if (fixed_delta_seconds > 0.0f) {
        glm_vec3_scale(world_accel, 1.0f / fixed_delta_seconds, world_accel);
    }
    glm_vec3_copy(horizontal_velocity, state->previous_horizontal_velocity);

    const float cos_yaw = cosf(body_yaw_radians);
    const float sin_yaw = sinf(body_yaw_radians);
    const float local_right_accel = -world_accel[0] * sin_yaw + world_accel[2] * cos_yaw;
    const float local_forward_accel = world_accel[0] * cos_yaw + world_accel[2] * sin_yaw;

    vec3 target_inertia_offset = {-local_right_accel * CAMERA_MOTION_INERTIA_GAIN_SECONDS, 0.0f,
                                   -local_forward_accel * CAMERA_MOTION_INERTIA_GAIN_SECONDS};
    const float target_inertia_magnitude = glm_vec3_norm(target_inertia_offset);
    if (target_inertia_magnitude > CAMERA_MOTION_INERTIA_MAX_OFFSET_METERS) {
        glm_vec3_scale(target_inertia_offset, CAMERA_MOTION_INERTIA_MAX_OFFSET_METERS / target_inertia_magnitude,
                       target_inertia_offset);
    }
    player_movement_approach_velocity(state->inertia_offset, target_inertia_offset,
                                       CAMERA_MOTION_INERTIA_SPRING_RATE_PER_SEC, fixed_delta_seconds,
                                       state->inertia_offset);
    vec3 inertia_scaled;
    glm_vec3_scale(state->inertia_offset, config->inertia_intensity, inertia_scaled);

    /* --- landing dip/recovery --- */
    if (landing_event_or_null != nullptr) {
        const float target_amplitude = landing_amplitude_for_severity(landing_event_or_null->severity);
        /* Blend with whatever's still in progress rather than
         * restarting additively - a new landing mid-recovery takes the
         * larger of the two, and extends recovery to the longer of the
         * two remaining durations. */
        const float current_magnitude = fabsf(state->landing_offset[1]);
        state->landing_peak_amplitude_meters = fmaxf(current_magnitude, target_amplitude);
        state->landing_recovery_total_seconds = CAMERA_MOTION_LANDING_RECOVERY_SECONDS;
        state->landing_recovery_seconds_remaining =
            fmaxf(state->landing_recovery_seconds_remaining, CAMERA_MOTION_LANDING_RECOVERY_SECONDS);
    }
    if (state->landing_recovery_seconds_remaining > 0.0f) {
        state->landing_recovery_seconds_remaining -= fixed_delta_seconds;
        if (state->landing_recovery_seconds_remaining < 0.0f) {
            state->landing_recovery_seconds_remaining = 0.0f;
        }
        const float total = fmaxf(state->landing_recovery_total_seconds, 1e-6f);
        const float t = 1.0f - (state->landing_recovery_seconds_remaining / total);
        const float eased = 1.0f - (1.0f - t) * (1.0f - t); /* ease-out quadratic: fast recoil, slow settle */
        state->landing_offset[1] = -state->landing_peak_amplitude_meters * (1.0f - eased);
    } else {
        state->landing_offset[1] = 0.0f;
        state->landing_peak_amplitude_meters = 0.0f;
    }
    vec3 landing_scaled;
    glm_vec3_scale(state->landing_offset, config->landing_intensity, landing_scaled);

    vec3 total_offset;
    glm_vec3_add(gait_offset, inertia_scaled, total_offset);
    glm_vec3_add(total_offset, landing_scaled, total_offset);
    glm_vec3_scale(total_offset, config->global_intensity, total_offset);
    glm_vec3_copy(total_offset, state->current_offset);
}

void camera_motion_get_render_offset(const CameraMotionState *state, float alpha, vec3 out_local_offset) {
    glm_vec3_lerp((float *)state->previous_offset, (float *)state->current_offset, clamp01(alpha),
                   out_local_offset);
}
