#include "player_movement.h"

#include "player_tuning.h"

#include <math.h>

/* Near-stationary threshold below which a current velocity is treated
 * as "no direction" rather than a real turn to classify or clamp
 * against - starting to move from a dead stop is acceleration, not a
 * turn or a curvature-limited redirect. */
#define PLAYER_MOVEMENT_STATIONARY_SPEED_METERS_PER_SEC 0.05f

StaminaState player_movement_update_stamina(StaminaState current, bool wants_sprint,
                                             bool has_move_input, bool sprint_context_allowed,
                                             float delta_seconds) {
    StaminaState next = current;
    const bool   is_sprinting = wants_sprint && has_move_input && sprint_context_allowed &&
                               current.stamina > 0.0f && !current.sprint_locked_out;

    if (is_sprinting) {
        next.stamina -= PLAYER_STAMINA_DRAIN_PER_SEC * delta_seconds;
        if (next.stamina <= 0.0f) {
            next.stamina = 0.0f;
            next.sprint_locked_out = true;
        }
        return next;
    }

    next.stamina += PLAYER_STAMINA_REGEN_PER_SEC * delta_seconds;
    if (next.stamina > PLAYER_STAMINA_MAX) {
        next.stamina = PLAYER_STAMINA_MAX;
    }
    if (next.sprint_locked_out && next.stamina >= PLAYER_STAMINA_SPRINT_REENABLE_THRESHOLD) {
        next.sprint_locked_out = false;
    }
    return next;
}

PlayerSpeedTier player_movement_select_speed_tier(bool wants_sprint, bool wants_walk_modifier,
                                                   bool sprint_locked_out, bool has_move_input,
                                                   bool sprint_context_allowed) {
    if (wants_sprint && has_move_input && sprint_context_allowed && !sprint_locked_out) {
        return PLAYER_SPEED_TIER_SPRINT;
    }
    if (wants_walk_modifier) {
        return PLAYER_SPEED_TIER_WALK;
    }
    return PLAYER_SPEED_TIER_RUN;
}

float player_movement_speed_for_tier(PlayerSpeedTier tier) {
    switch (tier) {
        case PLAYER_SPEED_TIER_WALK:
            return PLAYER_WALK_SPEED_METERS_PER_SEC;
        case PLAYER_SPEED_TIER_SPRINT:
            return PLAYER_SPRINT_SPEED_METERS_PER_SEC;
        case PLAYER_SPEED_TIER_CROUCH:
            return PLAYER_CROUCH_SPEED_METERS_PER_SEC;
        case PLAYER_SPEED_TIER_RUN:
        case PLAYER_SPEED_TIER_COUNT:
        default:
            return PLAYER_RUN_SPEED_METERS_PER_SEC;
    }
}

float player_movement_accel_for_tier(PlayerSpeedTier tier) {
    switch (tier) {
        case PLAYER_SPEED_TIER_WALK:
            return PLAYER_WALK_ACCEL_METERS_PER_SEC2;
        case PLAYER_SPEED_TIER_SPRINT:
            return PLAYER_SPRINT_ACCEL_METERS_PER_SEC2;
        case PLAYER_SPEED_TIER_CROUCH:
            return PLAYER_CROUCH_ACCEL_METERS_PER_SEC2;
        case PLAYER_SPEED_TIER_RUN:
        case PLAYER_SPEED_TIER_COUNT:
        default:
            return PLAYER_RUN_ACCEL_METERS_PER_SEC2;
    }
}

float player_movement_decel_for_tier(PlayerSpeedTier tier) {
    switch (tier) {
        case PLAYER_SPEED_TIER_WALK:
            return PLAYER_WALK_DECEL_METERS_PER_SEC2;
        case PLAYER_SPEED_TIER_SPRINT:
            return PLAYER_SPRINT_DECEL_METERS_PER_SEC2;
        case PLAYER_SPEED_TIER_CROUCH:
            return PLAYER_CROUCH_DECEL_METERS_PER_SEC2;
        case PLAYER_SPEED_TIER_RUN:
        case PLAYER_SPEED_TIER_COUNT:
        default:
            return PLAYER_RUN_DECEL_METERS_PER_SEC2;
    }
}

void player_movement_target_velocity(vec3 wish_direction_unit, float speed_meters_per_sec,
                                      vec3 out_target_velocity) {
    out_target_velocity[0] = wish_direction_unit[0] * speed_meters_per_sec;
    out_target_velocity[1] = 0.0f;
    out_target_velocity[2] = wish_direction_unit[2] * speed_meters_per_sec;
}

void player_movement_approach_velocity(vec3 current, vec3 target, float accel_meters_per_sec2,
                                        float delta_seconds, vec3 out_velocity) {
    vec3 diff;
    glm_vec3_sub(target, current, diff);
    const float diff_len = glm_vec3_norm(diff);
    const float max_step = accel_meters_per_sec2 * delta_seconds;

    if (diff_len <= max_step || diff_len == 0.0f) {
        glm_vec3_copy(target, out_velocity);
        return;
    }

    glm_vec3_scale(diff, max_step / diff_len, diff);
    glm_vec3_add(current, diff, out_velocity);
}

float player_movement_wrap_angle_radians(float angle_radians) {
    float wrapped = fmodf(angle_radians, 2.0f * GLM_PIf);
    if (wrapped > GLM_PIf) {
        wrapped -= 2.0f * GLM_PIf;
    } else if (wrapped <= -GLM_PIf) {
        wrapped += 2.0f * GLM_PIf;
    }
    return wrapped;
}

float player_movement_angle_difference_radians(float from_radians, float to_radians) {
    return player_movement_wrap_angle_radians(to_radians - from_radians);
}

float player_movement_yaw_from_direction(vec3 direction_xz) {
    return atan2f(direction_xz[2], direction_xz[0]);
}

float player_movement_resolve_body_yaw(float current_body_yaw_radians, float view_yaw_radians,
                                        float movement_yaw_radians, bool has_move_input,
                                        float turn_in_place_threshold_radians) {
    if (has_move_input) {
        return movement_yaw_radians;
    }
    const float divergence =
        player_movement_angle_difference_radians(current_body_yaw_radians, view_yaw_radians);
    if (fabsf(divergence) > turn_in_place_threshold_radians) {
        return view_yaw_radians;
    }
    return current_body_yaw_radians;
}

bool player_movement_is_within_cone(vec3 forward_direction_unit, vec3 wish_direction_unit,
                                     float half_angle_radians) {
    float dot = glm_vec3_dot(forward_direction_unit, wish_direction_unit);
    if (dot > 1.0f) {
        dot = 1.0f;
    } else if (dot < -1.0f) {
        dot = -1.0f;
    }
    return acosf(dot) <= half_angle_radians;
}

PlayerTurnSeverity player_movement_classify_turn(vec3 current_velocity, vec3 wish_direction_unit) {
    const float current_speed = glm_vec3_norm(current_velocity);
    if (current_speed < PLAYER_MOVEMENT_STATIONARY_SPEED_METERS_PER_SEC) {
        return PLAYER_TURN_SEVERITY_MINOR;
    }

    vec3 current_direction;
    glm_vec3_scale(current_velocity, 1.0f / current_speed, current_direction);
    float dot = glm_vec3_dot(current_direction, wish_direction_unit);
    if (dot > 1.0f) {
        dot = 1.0f;
    } else if (dot < -1.0f) {
        dot = -1.0f;
    }
    const float angle = acosf(dot);

    if (angle <= PLAYER_TURN_MODERATE_THRESHOLD_RADIANS) {
        return PLAYER_TURN_SEVERITY_MINOR;
    }
    if (angle <= PLAYER_TURN_SHARP_THRESHOLD_RADIANS) {
        return PLAYER_TURN_SEVERITY_MODERATE;
    }
    if (angle <= PLAYER_TURN_REVERSAL_THRESHOLD_RADIANS) {
        return PLAYER_TURN_SEVERITY_SHARP;
    }
    return PLAYER_TURN_SEVERITY_REVERSAL;
}

float player_movement_turn_speed_multiplier(PlayerTurnSeverity severity) {
    switch (severity) {
        case PLAYER_TURN_SEVERITY_MODERATE:
            return PLAYER_TURN_MODERATE_SPEED_MULTIPLIER;
        case PLAYER_TURN_SEVERITY_SHARP:
            return PLAYER_TURN_SHARP_SPEED_MULTIPLIER;
        case PLAYER_TURN_SEVERITY_REVERSAL:
            return PLAYER_TURN_REVERSAL_SPEED_MULTIPLIER;
        case PLAYER_TURN_SEVERITY_MINOR:
        default:
            return 1.0f;
    }
}

bool player_movement_clamp_sprint_turn(vec3 current_velocity, vec3 wish_direction_unit,
                                        float sprint_speed_meters_per_sec, float delta_seconds,
                                        vec3 out_clamped_direction_unit) {
    const float current_speed = glm_vec3_norm(current_velocity);
    if (current_speed < PLAYER_MOVEMENT_STATIONARY_SPEED_METERS_PER_SEC) {
        glm_vec3_copy(wish_direction_unit, out_clamped_direction_unit);
        return false;
    }

    vec3 current_direction;
    glm_vec3_scale(current_velocity, 1.0f / current_speed, current_direction);

    const float current_yaw = player_movement_yaw_from_direction(current_direction);
    const float wish_yaw = player_movement_yaw_from_direction(wish_direction_unit);
    const float diff = player_movement_angle_difference_radians(current_yaw, wish_yaw);

    float normalized_speed = current_speed / sprint_speed_meters_per_sec;
    if (normalized_speed > 1.0f) {
        normalized_speed = 1.0f;
    } else if (normalized_speed < 0.0f) {
        normalized_speed = 0.0f;
    }

    const float turn_rate =
        PLAYER_SPRINT_TURN_RATE_LOW_SPEED_RADIANS_PER_SEC +
        (PLAYER_SPRINT_TURN_RATE_FULL_SPEED_RADIANS_PER_SEC -
         PLAYER_SPRINT_TURN_RATE_LOW_SPEED_RADIANS_PER_SEC) *
            normalized_speed;
    const float max_step = turn_rate * delta_seconds;

    const bool exceeded = fabsf(diff) > max_step;
    float      clamped_diff = diff;
    if (clamped_diff > max_step) {
        clamped_diff = max_step;
    } else if (clamped_diff < -max_step) {
        clamped_diff = -max_step;
    }

    const float new_yaw = current_yaw + clamped_diff;
    out_clamped_direction_unit[0] = cosf(new_yaw);
    out_clamped_direction_unit[1] = 0.0f;
    out_clamped_direction_unit[2] = sinf(new_yaw);
    return exceeded;
}

float player_movement_slope_angle_radians(vec3 ground_normal) {
    vec3 up = {0.0f, 1.0f, 0.0f};
    float dot = glm_vec3_dot(ground_normal, up);
    if (dot > 1.0f) {
        dot = 1.0f;
    } else if (dot < -1.0f) {
        dot = -1.0f;
    }
    return acosf(dot);
}

bool player_movement_is_walkable_slope(float slope_angle_radians) {
    return slope_angle_radians <= PLAYER_WALKABLE_SLOPE_LIMIT_RADIANS;
}

float player_movement_uphill_component(vec3 ground_normal, vec3 wish_direction_unit) {
    /* The slope's steepest-ascent direction projected onto the ground
     * plane is (up - (up.normal)*normal), i.e. world up with the
     * component along the normal removed, then re-normalized. On flat
     * ground (normal == up) this degenerates to the zero vector, which
     * correctly yields a 0 uphill component regardless of direction. */
    vec3 up = {0.0f, 1.0f, 0.0f};
    const float up_dot_normal = glm_vec3_dot(up, ground_normal);
    vec3        uphill_direction;
    glm_vec3_scale(ground_normal, up_dot_normal, uphill_direction);
    glm_vec3_sub(up, uphill_direction, uphill_direction);

    const float uphill_len = glm_vec3_norm(uphill_direction);
    if (uphill_len < 1e-6f) {
        return 0.0f;
    }
    glm_vec3_scale(uphill_direction, 1.0f / uphill_len, uphill_direction);
    return glm_vec3_dot(uphill_direction, wish_direction_unit);
}

float player_movement_slope_speed_multiplier(PlayerSpeedTier tier, float slope_angle_radians,
                                              float uphill_component) {
    if (!player_movement_is_walkable_slope(slope_angle_radians)) {
        return 0.0f;
    }

    float max_penalty;
    switch (tier) {
        case PLAYER_SPEED_TIER_WALK:
            max_penalty = PLAYER_WALK_MAX_UPHILL_PENALTY;
            break;
        case PLAYER_SPEED_TIER_SPRINT:
            max_penalty = PLAYER_SPRINT_MAX_UPHILL_PENALTY;
            break;
        case PLAYER_SPEED_TIER_RUN:
        case PLAYER_SPEED_TIER_COUNT:
        default:
            max_penalty = PLAYER_RUN_MAX_UPHILL_PENALTY;
            break;
    }

    float clamped_uphill = uphill_component;
    if (clamped_uphill < 0.0f) {
        clamped_uphill = 0.0f;
    } else if (clamped_uphill > 1.0f) {
        clamped_uphill = 1.0f;
    }

    const float angle_fraction = slope_angle_radians / PLAYER_WALKABLE_SLOPE_LIMIT_RADIANS;
    return 1.0f - angle_fraction * max_penalty * clamped_uphill;
}

float player_movement_capsule_radius_for_stance(PlayerStance stance) {
    switch (stance) {
        case PLAYER_STANCE_CROUCHING:
            return PLAYER_CROUCHING_CAPSULE_RADIUS_METERS;
        case PLAYER_STANCE_STANDING:
        default:
            return PLAYER_STANDING_CAPSULE_RADIUS_METERS;
    }
}

float player_movement_capsule_half_height_for_stance(PlayerStance stance) {
    switch (stance) {
        case PLAYER_STANCE_CROUCHING:
            return PLAYER_CROUCHING_CAPSULE_HALF_HEIGHT_METERS;
        case PLAYER_STANCE_STANDING:
        default:
            return PLAYER_STANDING_CAPSULE_HALF_HEIGHT_METERS;
    }
}

float player_movement_eye_height_offset_for_stance(PlayerStance stance) {
    switch (stance) {
        case PLAYER_STANCE_CROUCHING:
            return PLAYER_CROUCHING_EYE_HEIGHT_OFFSET_METERS;
        case PLAYER_STANCE_STANDING:
        default:
            return PLAYER_STANDING_EYE_HEIGHT_OFFSET_METERS;
    }
}

float player_movement_approach_scalar(float current, float target, float rate_per_sec,
                                       float delta_seconds) {
    const float diff = target - current;
    const float max_step = rate_per_sec * delta_seconds;
    if (fabsf(diff) <= max_step) {
        return target;
    }
    return current + (diff > 0.0f ? max_step : -max_step);
}

PlayerLandingSeverity player_movement_classify_landing(float impact_speed_meters_per_sec) {
    if (impact_speed_meters_per_sec <= PLAYER_LANDING_SOFT_MAX_IMPACT_SPEED_METERS_PER_SEC) {
        return PLAYER_LANDING_SEVERITY_SOFT;
    }
    if (impact_speed_meters_per_sec <= PLAYER_LANDING_STANDARD_MAX_IMPACT_SPEED_METERS_PER_SEC) {
        return PLAYER_LANDING_SEVERITY_STANDARD;
    }
    if (impact_speed_meters_per_sec <= PLAYER_LANDING_HEAVY_MAX_IMPACT_SPEED_METERS_PER_SEC) {
        return PLAYER_LANDING_SEVERITY_HEAVY;
    }
    return PLAYER_LANDING_SEVERITY_DAMAGING;
}

float player_movement_landing_recovery_seconds(PlayerLandingSeverity severity) {
    const float span = PLAYER_LANDING_RECOVERY_MAX_SECONDS - PLAYER_LANDING_RECOVERY_MIN_SECONDS;
    switch (severity) {
        case PLAYER_LANDING_SEVERITY_SOFT:
            return PLAYER_LANDING_RECOVERY_MIN_SECONDS;
        case PLAYER_LANDING_SEVERITY_STANDARD:
            return PLAYER_LANDING_RECOVERY_MIN_SECONDS + span * (1.0f / 3.0f);
        case PLAYER_LANDING_SEVERITY_HEAVY:
            return PLAYER_LANDING_RECOVERY_MIN_SECONDS + span * (2.0f / 3.0f);
        case PLAYER_LANDING_SEVERITY_DAMAGING:
        default:
            return PLAYER_LANDING_RECOVERY_MAX_SECONDS;
    }
}

float player_movement_stride_length_for_gait(PlayerGait gait) {
    switch (gait.speed_tier) {
        case PLAYER_SPEED_TIER_WALK:
            return PLAYER_STRIDE_LENGTH_WALK_METERS;
        case PLAYER_SPEED_TIER_SPRINT:
            return PLAYER_STRIDE_LENGTH_SPRINT_METERS;
        case PLAYER_SPEED_TIER_CROUCH:
            return PLAYER_STRIDE_LENGTH_CROUCH_METERS;
        case PLAYER_SPEED_TIER_RUN:
        case PLAYER_SPEED_TIER_COUNT:
        default:
            return PLAYER_STRIDE_LENGTH_RUN_METERS;
    }
}

float player_movement_footstep_intensity_for_gait(PlayerGait gait) {
    switch (gait.speed_tier) {
        case PLAYER_SPEED_TIER_WALK:
            return PLAYER_FOOTSTEP_INTENSITY_WALK;
        case PLAYER_SPEED_TIER_SPRINT:
            return PLAYER_FOOTSTEP_INTENSITY_SPRINT;
        case PLAYER_SPEED_TIER_CROUCH:
            return PLAYER_FOOTSTEP_INTENSITY_CROUCH;
        case PLAYER_SPEED_TIER_RUN:
        case PLAYER_SPEED_TIER_COUNT:
        default:
            return PLAYER_FOOTSTEP_INTENSITY_RUN;
    }
}

PlayerGaitStrideState player_movement_gait_stride_state_initial(void) {
    return (PlayerGaitStrideState){.next_foot = PLAYER_FOOT_LEFT, .accumulated_meters = 0.0f};
}

void player_movement_gait_stride_state_reset(PlayerGaitStrideState *state) {
    *state = player_movement_gait_stride_state_initial();
}

uint32_t player_movement_gait_stride_advance(PlayerGaitStrideState *state,
                                              float planar_displacement_meters,
                                              float stride_length_meters, uint32_t max_contacts_per_tick,
                                              PlayerFoot *out_feet, uint32_t out_feet_capacity,
                                              uint32_t *out_dropped_count) {
    if (out_dropped_count != nullptr) {
        *out_dropped_count = 0;
    }
    if (stride_length_meters <= 0.0f) {
        return 0;
    }

    state->accumulated_meters += planar_displacement_meters;

    const uint32_t contacts_available =
        (uint32_t)floorf(state->accumulated_meters / stride_length_meters);
    if (contacts_available == 0) {
        return 0;
    }

    uint32_t emitted = contacts_available;
    if (emitted > max_contacts_per_tick) {
        emitted = max_contacts_per_tick;
    }
    if (emitted > out_feet_capacity) {
        emitted = out_feet_capacity;
    }

    for (uint32_t i = 0; i < contacts_available; ++i) {
        if (i < emitted && out_feet != nullptr) {
            out_feet[i] = state->next_foot;
        }
        state->next_foot = (state->next_foot == PLAYER_FOOT_LEFT) ? PLAYER_FOOT_RIGHT : PLAYER_FOOT_LEFT;
    }

    /* Every complete stride this displacement represents is consumed
     * here - emitted ones and dropped ones alike - so only the
     * fractional remainder survives (see the file header comment on
     * player_movement_gait_stride_advance() for why). */
    state->accumulated_meters -= (float)contacts_available * stride_length_meters;

    if (out_dropped_count != nullptr) {
        *out_dropped_count = contacts_available - emitted;
    }
    return emitted;
}

float player_movement_gait_stride_progress01(PlayerGaitStrideState state, float stride_length_meters) {
    if (stride_length_meters <= 0.0f) {
        return 0.0f;
    }
    float progress = state.accumulated_meters / stride_length_meters;
    if (progress < 0.0f) {
        progress = 0.0f;
    } else if (progress > 1.0f) {
        progress = 1.0f;
    }
    return progress;
}
