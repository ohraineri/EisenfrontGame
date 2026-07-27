/*
 * Player movement math - Infantry Controller.
 * Pure functions only: no Camera, CharacterController, PhysicsWorld or
 * Input dependency, so this logic is testable without a live engine
 * (see tests/player_movement_tests). infantry_entity.c is the only
 * caller and owns all engine-facing state; this module just does the
 * math, reading tuning values directly from player_tuning.h.
 */
#ifndef OUTPOST_PLAYER_MOVEMENT_H
#define OUTPOST_PLAYER_MOVEMENT_H

#include <cglm/cglm.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum PlayerSpeedTier {
    PLAYER_SPEED_TIER_WALK = 0,
    PLAYER_SPEED_TIER_RUN,
    PLAYER_SPEED_TIER_SPRINT,
    /* Single-speed locomotion state while crouched (spec 6's
     * "CrouchMove") - the caller forces this tier whenever the entity
     * is crouching, bypassing sprint/walk-modifier resolution
     * entirely; you cannot sprint while crouched. */
    PLAYER_SPEED_TIER_CROUCH,
    /* PLAYER_SPEED_TIER_ADS reserved for a future aim-down-sights
     * slow-walk tier - see player_tuning.h. */
    PLAYER_SPEED_TIER_COUNT
} PlayerSpeedTier;

typedef enum PlayerStance {
    PLAYER_STANCE_STANDING = 0,
    PLAYER_STANCE_CROUCHING,
    /* PLAYER_STANCE_PRONE reserved for a future prone locomotion mode
     * (spec Part XXII). */
} PlayerStance;

typedef enum PlayerTurnSeverity {
    PLAYER_TURN_SEVERITY_MINOR = 0,    /* 0-30 degrees, or starting from a stop */
    PLAYER_TURN_SEVERITY_MODERATE,     /* 30-70 degrees */
    PLAYER_TURN_SEVERITY_SHARP,        /* 70-120 degrees */
    PLAYER_TURN_SEVERITY_REVERSAL,     /* 120-180 degrees */
} PlayerTurnSeverity;

/* Airborne/landing sub-state (spec 6's Airborne{JumpAscending,Falling}
 * plus a LandingRecovery phase folded in here rather than kept
 * separate from Grounded, since both only ever matter to the same
 * vertical-integration code path). */
typedef enum PlayerVerticalState {
    PLAYER_VERTICAL_STATE_GROUNDED = 0,
    PLAYER_VERTICAL_STATE_JUMP_ASCENDING,
    PLAYER_VERTICAL_STATE_FALLING,
    PLAYER_VERTICAL_STATE_LANDING_RECOVERY,
} PlayerVerticalState;

/* Landing severity by vertical impact speed (spec 19). Incapacitating/
 * fatal impact is explicitly resolved by a Health system this engine
 * doesn't have yet - not represented here; DAMAGING is the ceiling. */
typedef enum PlayerLandingSeverity {
    PLAYER_LANDING_SEVERITY_SOFT = 0,
    PLAYER_LANDING_SEVERITY_STANDARD,
    PLAYER_LANDING_SEVERITY_HEAVY,
    PLAYER_LANDING_SEVERITY_DAMAGING,
} PlayerLandingSeverity;

typedef struct StaminaState {
    float stamina;         /* 0..PLAYER_STAMINA_MAX */
    bool  sprint_locked_out;
} StaminaState;

/* One tick of stamina drain/regen/lockout resolution.
 * has_move_input: sprint only drains while actually moving - holding
 * shift at a standstill is free.
 * sprint_context_allowed: false while airborne or outside the sprint
 * cone past its grace period (see player_movement_is_within_cone) -
 * those conditions end sprint without draining/regenerating any
 * differently than simply not holding sprint.
 * Once stamina fully depletes, sprint_locked_out latches true and
 * only clears after stamina regenerates back up to
 * PLAYER_STAMINA_SPRINT_REENABLE_THRESHOLD, preventing sprint from
 * sputtering on/off right at the depletion boundary. */
StaminaState player_movement_update_stamina(StaminaState current, bool wants_sprint,
                                             bool has_move_input, bool sprint_context_allowed,
                                             float delta_seconds);

/* Resolves the active tier. Base gait (no modifiers held) is Run;
 * wants_walk_modifier drops to the precision Walk gait; wants_sprint
 * takes priority over both when eligible (grounded, moving, within
 * the sprint cone's grace period, and not stamina-locked-out). */
PlayerSpeedTier player_movement_select_speed_tier(bool wants_sprint, bool wants_walk_modifier,
                                                   bool sprint_locked_out, bool has_move_input,
                                                   bool sprint_context_allowed);

float player_movement_speed_for_tier(PlayerSpeedTier tier);
float player_movement_accel_for_tier(PlayerSpeedTier tier);
float player_movement_decel_for_tier(PlayerSpeedTier tier);

/* target = wish_direction_unit * speed_meters_per_sec (Y always 0). */
void player_movement_target_velocity(vec3 wish_direction_unit, float speed_meters_per_sec,
                                      vec3 out_target_velocity);

/* Steps current toward target by at most accel_meters_per_sec2 * delta_seconds,
 * without overshoot - deterministic linear approach, not exponential decay. */
void player_movement_approach_velocity(vec3 current, vec3 target, float accel_meters_per_sec2,
                                        float delta_seconds, vec3 out_velocity);

/* Wraps to (-PI, PI]. */
float player_movement_wrap_angle_radians(float angle_radians);

/* Smallest signed angle from `from` to `to`, wrapped to (-PI, PI] -
 * positive means `to` is counterclockwise from `from`. */
float player_movement_angle_difference_radians(float from_radians, float to_radians);

/* Yaw angle (matching Camera.yaw_radians' convention: forward =
 * (cos(yaw), *, sin(yaw))) of a flattened XZ direction vector. Zero
 * vector yields 0.0f. */
float player_movement_yaw_from_direction(vec3 direction_xz);

/* Resolves this tick's body yaw (Infantry Controller spec 4.4/11):
 * body orientation is independent of view/camera yaw. While moving,
 * the body aligns to the movement direction. While stationary, the
 * body holds its current yaw unless it has diverged from view_yaw by
 * more than turn_in_place_threshold_radians, in which case it snaps to
 * match view_yaw (turn-in-place) - this never moves the collision
 * capsule, only the yaw value. */
float player_movement_resolve_body_yaw(float current_body_yaw_radians, float view_yaw_radians,
                                        float movement_yaw_radians, bool has_move_input,
                                        float turn_in_place_threshold_radians);

/* True if wish_direction_unit is within half_angle_radians of
 * forward_direction_unit (both flattened unit vectors) - Infantry
 * Controller spec 7.1's sprint cone. */
bool player_movement_is_within_cone(vec3 forward_direction_unit, vec3 wish_direction_unit,
                                     float half_angle_radians);

/* Classifies the angle between current horizontal velocity's direction
 * and the desired wish direction (spec 10). A near-stationary current
 * velocity (below a small epsilon) is always MINOR - starting from a
 * stop is acceleration, not a turn. */
PlayerTurnSeverity player_movement_classify_turn(vec3 current_velocity, vec3 wish_direction_unit);

/* Target-speed multiplier for this tick's turn severity - 1.0 for
 * MINOR, progressively lower for sharper turns, 0.0 for REVERSAL
 * (forces braking to a stop via the normal approach_velocity path
 * before the next tick can accelerate in the new direction - see
 * player_movement.c's file header comment on why no separate state
 * machine is needed for this). */
float player_movement_turn_speed_multiplier(PlayerTurnSeverity severity);

/* Curvature-constrained steering while sprinting (spec 10.2): clamps
 * how far the velocity direction may rotate this tick toward
 * wish_direction_unit, interpolating the max turn rate between
 * PLAYER_SPRINT_TURN_RATE_LOW_SPEED_RADIANS_PER_SEC (near-zero speed)
 * and PLAYER_SPRINT_TURN_RATE_FULL_SPEED_RADIANS_PER_SEC (at
 * sprint_speed_meters_per_sec). Returns true if the requested turn
 * exceeded what curvature allows this tick (caller should demote to
 * Run rather than force an unnaturally sharp sprint turn). If
 * current_velocity is near-stationary, there is no existing direction
 * to clamp against: wish_direction_unit is returned unclamped and this
 * returns false. */
bool player_movement_clamp_sprint_turn(vec3 current_velocity, vec3 wish_direction_unit,
                                        float sprint_speed_meters_per_sec, float delta_seconds,
                                        vec3 out_clamped_direction_unit);

/* Angle between ground_normal and world up, in radians - 0 for flat
 * ground. Currently always 0 in practice (see player_tuning.h's file
 * header comment on why), but this and the functions below are the
 * seam Game-side slope logic (spec 14) consumes once a sloped/ramp
 * shape exists in Physics. */
float player_movement_slope_angle_radians(vec3 ground_normal);

bool player_movement_is_walkable_slope(float slope_angle_radians);

/* How much of wish_direction_unit points uphill along the slope,
 * projected onto the ground plane: positive is uphill, negative is
 * downhill, 0 is flat or directly cross-slope. Not clamped to [0,1] -
 * callers that only care about the uphill penalty clamp it themselves
 * (see player_movement_slope_speed_multiplier). */
float player_movement_uphill_component(vec3 ground_normal, vec3 wish_direction_unit);

/* Speed multiplier for this gait given the slope angle and how much of
 * the wish direction is uphill (spec 14): 0 for a non-walkable slope,
 * otherwise linearly interpolated from 1.0 at 0 degrees down to
 * (1.0 - the gait's max uphill penalty) at the walkable limit,
 * scaled by the clamped-to-[0,1] uphill component so cross-slope and
 * downhill movement isn't penalized as steep ascent. */
float player_movement_slope_speed_multiplier(PlayerSpeedTier tier, float slope_angle_radians,
                                              float uphill_component);

/* Capsule/eye-height dims for a stance (spec 4.2's reference table). */
float player_movement_capsule_radius_for_stance(PlayerStance stance);
float player_movement_capsule_half_height_for_stance(PlayerStance stance);
float player_movement_eye_height_offset_for_stance(PlayerStance stance);

/* Steps a scalar toward target by at most rate_per_sec * delta_seconds,
 * without overshoot - the scalar counterpart to
 * player_movement_approach_velocity(), used to blend eye height
 * smoothly across a stance transition. */
float player_movement_approach_scalar(float current, float target, float rate_per_sec,
                                       float delta_seconds);

/* Classifies a landing by vertical impact speed (spec 19). */
PlayerLandingSeverity player_movement_classify_landing(float impact_speed_meters_per_sec);

/* Full-control-recovery duration for a landing severity - linearly
 * interpolates PLAYER_LANDING_RECOVERY_MIN_SECONDS (SOFT) through
 * PLAYER_LANDING_RECOVERY_MAX_SECONDS (DAMAGING) across the four bands. */
float player_movement_landing_recovery_seconds(PlayerLandingSeverity severity);

/* --- Gait / foot-contact stride (spec 25, M10) ---
 * Logical per-foot identity, independent of any skeleton/animation
 * system (none exists yet - a future one corrects/resynchronizes this
 * phase, it does not originate it). */
typedef enum PlayerFoot {
    PLAYER_FOOT_LEFT = 0,
    PLAYER_FOOT_RIGHT,
} PlayerFoot;

/* Explicit typed fields rather than a packed/ambiguous value - stance
 * and speed tier are independent axes today (tier already collapses to
 * PLAYER_SPEED_TIER_CROUCH while crouching) but are kept separate here
 * so a future stance (e.g. prone) can vary stride/intensity
 * independently of tier without reshaping every consumer. */
typedef struct PlayerGait {
    PlayerStance    stance;
    PlayerSpeedTier speed_tier;
} PlayerGait;

/* Data-driven stride length / normalized footstep intensity for a gait -
 * see player_tuning.h's PLAYER_STRIDE_LENGTH_ and PLAYER_FOOTSTEP_INTENSITY_
 * constants. */
float player_movement_stride_length_for_gait(PlayerGait gait);
float player_movement_footstep_intensity_for_gait(PlayerGait gait);

/* Deterministic alternating-foot stride accumulator. next_foot always
 * starts (and resets to) LEFT - the one documented initial-foot rule;
 * never randomized. */
typedef struct PlayerGaitStrideState {
    PlayerFoot next_foot;
    float      accumulated_meters;
} PlayerGaitStrideState;

PlayerGaitStrideState player_movement_gait_stride_state_initial(void);
void                  player_movement_gait_stride_state_reset(PlayerGaitStrideState *state);

/* Advances stride accumulation by planar_displacement_meters - the
 * caller's ACTUAL realized grounded planar movement for one fixed tick
 * (never requested input or raw velocity; a wall-blocked tick should
 * pass ~0 and produce nothing). Writes up to max_contacts_per_tick
 * alternating feet into out_feet (capacity out_feet_capacity) and
 * returns how many were written.
 *
 * Drop policy once max_contacts_per_tick is reached: every complete
 * stride this call's displacement represents - both the ones written to
 * out_feet and any additional complete strides beyond the cap - is
 * consumed from the accumulator (next_foot still alternates through all
 * of them, keeping phase parity correct), so only the fractional
 * remainder below one stride length survives into the next call.
 * Dropped whole strides are counted into *out_dropped_count (may be
 * nullptr). This means a single oversized tick never queues a delayed
 * burst for a later, stationary tick to surface - the distance is spent
 * (accounted for, not emitted), not carried forward. */
uint32_t player_movement_gait_stride_advance(PlayerGaitStrideState *state,
                                              float planar_displacement_meters,
                                              float stride_length_meters, uint32_t max_contacts_per_tick,
                                              PlayerFoot *out_feet, uint32_t out_feet_capacity,
                                              uint32_t *out_dropped_count);

/* Normalized progress toward the NEXT contact - 0 right after a contact,
 * approaching 1 as accumulated_meters approaches stride_length_meters.
 * Always in [0,1) immediately after player_movement_gait_stride_advance()
 * runs (it never leaves a whole stride's worth of distance sitting in
 * accumulated_meters), but this clamps defensively anyway: a gait change
 * can shrink stride_length_meters between calls (e.g. Sprint -> Crouch)
 * such that a still-valid accumulated_meters value would otherwise
 * exceed 1.0 against the NEW, shorter stride length. */
float player_movement_gait_stride_progress01(PlayerGaitStrideState state, float stride_length_meters);

/* --- M11: read-only gait/stride snapshot for presentation consumers
 * (camera_motion.c) ---
 * PlayerGaitStrideState (above) remains the single source of truth for
 * logical gait phase - this is a read-only VIEW of it, never a second
 * odometer. A presentation consumer must derive everything it needs
 * (bob phase, amplitude gating) from this snapshot's fields and must
 * never accumulate its own distance. */
typedef struct PlayerGaitPresentationSnapshot {
    PlayerFoot foot_phase;        /* the foot that will contact at progress01 == 1 (== PlayerGaitStrideState.next_foot) */
    float      stride_progress01; /* see player_movement_gait_stride_progress01() */
    float      stride_length_meters;
    PlayerGait gait;
    /* False while airborne, idle (below PLAYER_GAIT_MIN_PLANAR_SPEED_METERS_PER_SEC),
     * or on the exact tick a landing resets gait - presentation bob must
     * produce no motion (fading out, not snapping - see
     * CAMERA_MOTION_GAIT_ACTIVE_BLEND_RATE_PER_SEC) whenever this is false. */
    bool       motion_valid;
    /* Bumped by infantry_entity_reset_gait() and once at entity creation
     * - any change between two snapshots is a discontinuity (teleport,
     * noclip toggle, spawn) presentation consumers must resync across
     * rather than interpolate through. */
    uint32_t   reset_generation;
} PlayerGaitPresentationSnapshot;

#endif /* OUTPOST_PLAYER_MOVEMENT_H */
