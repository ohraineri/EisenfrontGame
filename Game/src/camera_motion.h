/*
 * Camera motion (Infantry Controller spec Part VI, M11) - presentation
 * only. Everything this module produces is an ADDITIVE offset in the
 * player's local eye-space (x=right, y=up, z=forward), applied by the
 * caller to a LOCAL COPY of the render Camera, never written back into
 * InfantryViewComponent.camera. That component stays the single
 * authoritative eye/aim transform - movement input, weapon shot
 * direction, and (once it exists) network state all read it unmodified.
 * This module never touches collision, capsule size, position, velocity,
 * or view yaw/pitch; it has no access to any of those to begin with.
 *
 * Pipeline (all channels are position offsets, summed):
 *   stance eye anchor (M6, upstream of this module, untouched)
 *     -> gait bob
 *     -> start/stop/turn inertia
 *     -> landing dip/recovery
 *     -> [reserved: recoil/suppression - no weapon system yet]
 *     -> accessibility intensity scale
 *     -> final render transform (caller's job)
 *
 * Gait phase: PlayerGaitStrideState (player_movement.h, driven by M10's
 * infantry_entity.c) remains the SINGLE odometer. This module only ever
 * reads a PlayerGaitPresentationSnapshot - it never accumulates its own
 * distance. One bob cycle = one single-foot stride (not a full L+R
 * pair) - see camera_motion.c's file header comment for why that's the
 * physically-correct frequency for vertical bob.
 *
 * Frame-rate independence: CameraMotionState keeps a previous/current
 * fixed-tick offset pair. Rendering interpolates between them with
 * alpha = clamp(sim_clock.accumulator_seconds / SIM_FIXED_DELTA_SECONDS, 0, 1)
 * - never extrapolates past the newest simulated tick. Discontinuities
 * (spawn, teleport, noclip toggle) are detected via
 * PlayerGaitPresentationSnapshot.reset_generation and force previous =
 * current (a hard snap, not an interpolated jump) instead of blending
 * across invalid history.
 */
#ifndef OUTPOST_CAMERA_MOTION_H
#define OUTPOST_CAMERA_MOTION_H

#include "infantry_fall_landing_event.h"
#include "player_movement.h"

#include <cglm/cglm.h>

/* Each 0..1; 0 disables that channel's procedural offset exactly (not
 * merely "small" - the computed offset is bitwise zero). global_intensity
 * multiplies on top of every channel below it. */
typedef struct CameraMotionConfig {
    float global_intensity;
    float gait_intensity;
    float landing_intensity;
    float inertia_intensity;
} CameraMotionConfig;

CameraMotionConfig camera_motion_config_default(void);

/* Pure - the underlying bob curves, exposed directly for testing the
 * extrema-coherence requirement without needing a real ticking entity.
 * One bob cycle per single-foot stride (see this file's own header
 * comment); zero at progress01 == 0 and 1 (footfall instants), peak at
 * 0.5. Lateral's sign is which foot is currently pending (documented,
 * arbitrary but fixed: LEFT negative, RIGHT positive) - never random,
 * never independent of foot_phase. */
float camera_motion_gait_vertical_offset(float stride_progress01, float amplitude_meters);
float camera_motion_gait_lateral_offset(PlayerFoot foot_phase, float stride_progress01, float amplitude_meters);

typedef struct CameraMotionState {
    vec3 previous_offset; /* local eye-space, end of the PREVIOUS fixed tick */
    vec3 current_offset;  /* local eye-space, end of the CURRENT fixed tick */

    /* --- gait bob running state --- */
    float gait_active_blend;           /* 0..1, fades bob in/out - see CAMERA_MOTION_GAIT_ACTIVE_BLEND_RATE_PER_SEC */
    float gait_vertical_amplitude_meters; /* blends toward the active gait's target */
    float gait_lateral_amplitude_meters;

    /* --- inertia running state --- */
    vec3 inertia_offset;                /* local eye-space, spring-damped */
    vec3 previous_horizontal_velocity;  /* world-space XZ, for computing realized acceleration */

    /* --- landing running state --- */
    vec3  landing_offset;               /* local eye-space, y-only in practice */
    float landing_peak_amplitude_meters;
    float landing_recovery_seconds_remaining;
    float landing_recovery_total_seconds;

    uint32_t last_seen_gait_reset_generation;
} CameraMotionState;

/* Neutral state (zero offsets, zero running state) - call once at
 * entity creation. */
void camera_motion_state_init(CameraMotionState *state);

/* Hard-resets to neutral (previous = current = zero, all running decay/
 * lag state cleared) - call at every discontinuity: teleport, noclip
 * toggle, spawn, respawn, or any future large authoritative correction.
 * Also called automatically by camera_motion_update_fixed() whenever it
 * observes PlayerGaitPresentationSnapshot.reset_generation change, so
 * callers that already invoke infantry_entity_reset_gait() at a given
 * site (debug_overlay.c's teleport button, main.c's smoke-test waypoint
 * branch, the internal noclip-toggle handling) get this for free - this
 * function is exposed for the sites that need it BEFORE a
 * reset_generation bump would otherwise be observed (main.c calls it
 * explicitly at entity creation, since there is no "previous" snapshot
 * yet to compare against). */
void camera_motion_reset(CameraMotionState *state);

/* One fixed-timestep update. horizontal_velocity is locomotion's
 * CURRENT world-space XZ velocity (Y always 0) - used only to derive
 * realized acceleration for inertia, never modified. body_yaw_radians is
 * the authoritative body orientation (read-only), used to convert that
 * world-space acceleration into the local eye-space frame this module's
 * offsets are expressed in. landing_event_or_null should be whatever
 * infantry_entity_pop_landing_event() returned this tick (nullptr most
 * ticks - landings are rare). */
void camera_motion_update_fixed(CameraMotionState *state, const CameraMotionConfig *config,
                                 PlayerGaitPresentationSnapshot snapshot, vec3 horizontal_velocity,
                                 float body_yaw_radians, const InfantryFallLandingEvent *landing_event_or_null,
                                 float fixed_delta_seconds);

/* Render-rate query: linearly interpolates previous_offset -> current_offset
 * by alpha (caller computes
 * clamp(sim_clock.accumulator_seconds / SIM_FIXED_DELTA_SECONDS, 0, 1) -
 * this function clamps defensively too). Never advances any state - safe
 * to call zero, one, or many times per render frame. */
void camera_motion_get_render_offset(const CameraMotionState *state, float alpha, vec3 out_local_offset);

#endif /* OUTPOST_CAMERA_MOTION_H */
