/*
 * Player movement tuning constants - Infantry Controller.
 * Single source of truth for infantry_entity.c and player_movement.c
 * so tuning a value never means hunting down a scattered magic number.
 */
#ifndef OUTPOST_PLAYER_TUNING_H
#define OUTPOST_PLAYER_TUNING_H

/* --- Vertical motion ---
 * Spec 17's jump table gives an absolute takeoff velocity (3.2 m/s)
 * and target rise (0.50-0.55m), calibrated together against a real-
 * world ~9.8 m/s^2 gravity. This engine's gravity (-20.0, set in
 * Phase 5 for arcade jump feel, unrelated to this spec and shipped
 * through six prior milestones) is roughly double that, so copying
 * the literal 3.2 m/s would produce a rise of ~0.26m - well short of
 * spec's range. Retuned instead to hit the RISE target under the
 * EXISTING gravity: rise = v^2/(2*g) => v = sqrt(2*20*0.529) = 4.6,
 * landing at rise=0.529m (target 0.50-0.55m). Gravity itself is left
 * unchanged - it's a site-wide constant (falling off ledges, all
 * vertical motion), not something this movement-feel milestone should
 * silently alter. */
#define PLAYER_GRAVITY_UNITS_PER_SEC2                 -20.0f
#define PLAYER_JUMP_SPEED_METERS_PER_SEC                4.6f
#define PLAYER_GROUNDED_STICK_VELOCITY_UNITS_PER_SEC   -0.5f
#define PLAYER_JUMP_STAMINA_COST                       10.0f /* spec 17's "data-driven" jump stamina cost */
#define PLAYER_JUMP_LOCKOUT_SECONDS                     0.45f /* repeat-jump lockout, applied at landing (spec 17/17.3) */

/* --- Landing (spec 19) - severity bands by vertical impact speed and
 * the resulting full-control-recovery duration. Spec doesn't give
 * exact impact-speed thresholds (left to production tuning/a Health
 * system this engine doesn't have) - these are simple, deterministic
 * placeholders documented as such, not measured against real playtest
 * data. Recovery duration linearly interpolates spec 17's given range
 * (0.20-0.35s) across the four bands. --- */
#define PLAYER_LANDING_SOFT_MAX_IMPACT_SPEED_METERS_PER_SEC      2.0f
#define PLAYER_LANDING_STANDARD_MAX_IMPACT_SPEED_METERS_PER_SEC  5.0f
#define PLAYER_LANDING_HEAVY_MAX_IMPACT_SPEED_METERS_PER_SEC     8.0f
/* above HEAVY -> DAMAGING (spec's Incapacitating/fatal tier is explicitly
 * resolved by a Health system - not represented; see player_movement.h). */
#define PLAYER_LANDING_RECOVERY_MIN_SECONDS              0.20f
#define PLAYER_LANDING_RECOVERY_MAX_SECONDS              0.35f
#define PLAYER_LANDING_RECOVERY_ACCEL_METERS_PER_SEC2    2.0f

/* --- Speed tiers (Infantry Controller spec 7.1) ---
 * Absolute per-gait speeds, not a multiplier of a shared base - Walk,
 * Run and Sprint are independent named gaits. Chosen to land within
 * spec 9's target stopping-distance ranges given the braking constants
 * below (constant-deceleration stopping distance d = v^2 / (2*decel)):
 *   Walk:   1.5 m/s, decel 10.0 -> d=0.1125m  (target 0.08-0.16m)
 *   Run:    3.0 m/s, decel  8.5 -> d=0.529m   (target 0.35-0.65m)
 *   Sprint: 4.4 m/s, decel  7.0 -> d=1.383m   (target 1.05-1.55m)
 */
#define PLAYER_WALK_SPEED_METERS_PER_SEC                 1.5f
#define PLAYER_RUN_SPEED_METERS_PER_SEC                  3.0f
#define PLAYER_SPRINT_SPEED_METERS_PER_SEC               4.4f
/* PLAYER_ADS_SPEED_METERS_PER_SEC reserved for a future aim-down-
 * sights slow-walk tier (expected < PLAYER_WALK_SPEED_METERS_PER_SEC).
 * Not implemented this pass - no ADS system exists yet. When added:
 * extend PlayerSpeedTier, give ADS its own speed here, and extend
 * player_movement_select_speed_tier() (ADS should presumably take
 * priority over sprint). */

#define PLAYER_NOCLIP_BASE_SPEED_UNITS_PER_SEC           5.0f /* debug-only fly speed baseline, unrelated to the gait speeds above */
#define PLAYER_NOCLIP_SPEED_MULTIPLIER                   3.0f

/* --- Per-gait acceleration (spec 8) - strongest at Walk, weakest at
 * Sprint (sprint takes longest to redirect, matching momentum
 * commitment), plus a small corrective-only cap while airborne. --- */
#define PLAYER_WALK_ACCEL_METERS_PER_SEC2                8.0f
#define PLAYER_RUN_ACCEL_METERS_PER_SEC2                 6.5f
#define PLAYER_SPRINT_ACCEL_METERS_PER_SEC2              4.8f
#define PLAYER_AIRBORNE_ACCEL_METERS_PER_SEC2            0.35f

/* --- Per-gait braking (spec 9) --- */
#define PLAYER_WALK_DECEL_METERS_PER_SEC2               10.0f
#define PLAYER_RUN_DECEL_METERS_PER_SEC2                 8.5f
#define PLAYER_SPRINT_DECEL_METERS_PER_SEC2              7.0f

/* --- Sprint cone + curvature-constrained turning (spec 7.1/10.2) --- */
#define PLAYER_SPRINT_CONE_HALF_ANGLE_RADIANS            0.95993109f /* 55 degrees */
#define PLAYER_SPRINT_CONE_GRACE_SECONDS                 0.25f
#define PLAYER_SPRINT_TURN_RATE_LOW_SPEED_RADIANS_PER_SEC  1.48352986f /* 85 deg/s */
#define PLAYER_SPRINT_TURN_RATE_FULL_SPEED_RADIANS_PER_SEC 0.95993109f /* 55 deg/s */

/* --- Direction-change severity (spec 10/10.1) - classified by the
 * angle between current velocity and the desired direction; sharper
 * turns cost speed, and a near-full reversal costs all of it (braking
 * to a stop before building speed the other way), which is also what
 * makes rapid strafing/zig-zagging physically unrewarding without any
 * cooldown or lockout. --- */
#define PLAYER_TURN_MODERATE_THRESHOLD_RADIANS           0.52359878f /* 30 degrees */
#define PLAYER_TURN_SHARP_THRESHOLD_RADIANS              1.22173048f /* 70 degrees */
#define PLAYER_TURN_REVERSAL_THRESHOLD_RADIANS           2.09439510f /* 120 degrees */
#define PLAYER_TURN_MODERATE_SPEED_MULTIPLIER            0.85f
#define PLAYER_TURN_SHARP_SPEED_MULTIPLIER               0.6f
#define PLAYER_TURN_REVERSAL_SPEED_MULTIPLIER            0.0f

/* --- Stamina (points, not normalized 0..1, for readable debug output) --- */
#define PLAYER_STAMINA_MAX                             100.0f
#define PLAYER_STAMINA_DRAIN_PER_SEC                    25.0f /* full drain after 4s of continuous sprint */
#define PLAYER_STAMINA_REGEN_PER_SEC                    15.0f /* full regen from 0 in ~6.7s */
#define PLAYER_STAMINA_SPRINT_REENABLE_THRESHOLD        20.0f /* must regen back up to here after hitting 0 before sprint is allowed again */

/* --- Body/view yaw separation --- */
#define PLAYER_TURN_IN_PLACE_THRESHOLD_RADIANS           1.13446401f /* 65 degrees - view/body divergence beyond this triggers turn-in-place */

/* --- Slopes (spec 14) - policy layer only. Currently unreachable in
 * live gameplay: Physics' box shapes are always world-axis-aligned
 * (see physics.h's file header comment), so no sloped/ramp collision
 * surface can be constructed yet and character_controller_get_ground_normal()
 * always reports (0,1,0) in this scene. Wired in now (rather than left
 * out) so it activates automatically the moment a ramp/oriented shape
 * is added to Physics - a separate, larger engine milestone. --- */
#define PLAYER_WALKABLE_SLOPE_LIMIT_RADIANS              0.78539816f /* 45 degrees */
#define PLAYER_SPRINT_DISABLE_SLOPE_ANGLE_RADIANS         0.61086524f /* 35 degrees */
#define PLAYER_WALK_MAX_UPHILL_PENALTY                   0.20f
#define PLAYER_RUN_MAX_UPHILL_PENALTY                     0.30f
#define PLAYER_SPRINT_MAX_UPHILL_PENALTY                  0.45f

/* --- Stance / crouch (spec Part IX, 4.2) - capsule dims and eye
 * heights match spec 4.2's reference table exactly (both stances use
 * the same radius; only height changes). Crouch speed/accel are
 * spec-given; crouch braking isn't in spec 9's table, so a value
 * between Walk's (10.0, most controlled) and Run's (8.5) is used,
 * matching crouch's precise-but-slower character. --- */
#define PLAYER_STANDING_CAPSULE_RADIUS_METERS            0.32f
#define PLAYER_STANDING_CAPSULE_HALF_HEIGHT_METERS       0.58f  /* total height 1.80m */
#define PLAYER_STANDING_EYE_HEIGHT_OFFSET_METERS         0.77f  /* 1.67m above ground, above capsule center */
#define PLAYER_CROUCHING_CAPSULE_RADIUS_METERS           0.32f
#define PLAYER_CROUCHING_CAPSULE_HALF_HEIGHT_METERS      0.305f /* total height 1.25m */
#define PLAYER_CROUCHING_EYE_HEIGHT_OFFSET_METERS        0.495f /* 1.12m above ground, above capsule center */

#define PLAYER_CROUCH_SPEED_METERS_PER_SEC               1.0f
#define PLAYER_CROUCH_ACCEL_METERS_PER_SEC2              5.5f
#define PLAYER_CROUCH_DECEL_METERS_PER_SEC2              9.0f

/* Minimum time between stance changes (spec 20.3's anti-spam
 * requirement - the collision resize itself is always instant/atomic,
 * this only rate-limits how often a NEW change may be attempted) and
 * the rate the eye height blends toward its target over that same
 * window ((standing - crouching) / duration). */
#define PLAYER_STANCE_TRANSITION_SECONDS                 0.25f
#define PLAYER_STANCE_EYE_HEIGHT_BLEND_RATE_METERS_PER_SEC \
    ((PLAYER_STANDING_EYE_HEIGHT_OFFSET_METERS - PLAYER_CROUCHING_EYE_HEIGHT_OFFSET_METERS) / \
     PLAYER_STANCE_TRANSITION_SECONDS)

/* --- Gait / foot-contact stride (spec 25, M10) - data-driven per gait
 * so tuning cadence never means hunting a magic number in
 * infantry_entity.c. Stride length is the real planar distance between
 * one foot-contact and the next; intensity is a normalized 0..1 value
 * carried on InfantryFootContactEvent for consumers (audio today) to
 * scale playback by. Neither is spec-measured - simple, deterministic
 * placeholders documented as such, same spirit as the landing-severity
 * bands above. --- */
#define PLAYER_STRIDE_LENGTH_WALK_METERS                 0.75f
#define PLAYER_STRIDE_LENGTH_RUN_METERS                  1.10f
#define PLAYER_STRIDE_LENGTH_SPRINT_METERS               1.35f
#define PLAYER_STRIDE_LENGTH_CROUCH_METERS               0.55f

#define PLAYER_FOOTSTEP_INTENSITY_WALK                   0.45f
#define PLAYER_FOOTSTEP_INTENSITY_RUN                    0.70f
#define PLAYER_FOOTSTEP_INTENSITY_SPRINT                 1.00f
#define PLAYER_FOOTSTEP_INTENSITY_CROUCH                 0.25f

/* Below this planar speed, the entity is treated as stationary for gait
 * purposes (phase-reset idle timer) - matches player_movement.c's own
 * "near stationary" turn-classification threshold. */
#define PLAYER_GAIT_MIN_PLANAR_SPEED_METERS_PER_SEC      0.05f
/* Phase resets to the documented initial foot after this much
 * continuous time below the speed threshold above. */
#define PLAYER_GAIT_IDLE_RESET_SECONDS                   0.5f
/* Safety cap on how many foot-contact events a single fixed tick may
 * produce - see player_movement_gait_stride_advance()'s file header
 * comment on the drop policy once this is reached. */
#define PLAYER_MAX_FOOT_CONTACTS_PER_TICK                2u

#endif /* OUTPOST_PLAYER_TUNING_H */
