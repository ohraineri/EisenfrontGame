/*
 * Camera-motion tuning constants (Infantry Controller spec Part VI,
 * M11) - kept separate from player_tuning.h deliberately: everything
 * here is presentation-only (never read by movement/collision/network
 * code), while player_tuning.h is authoritative gameplay tuning.
 */
#ifndef OUTPOST_CAMERA_MOTION_TUNING_H
#define OUTPOST_CAMERA_MOTION_TUNING_H

/* --- Gait bob amplitudes (spec's baseline maximums, in meters) - one
 * full bob cycle per single-foot stride (see camera_motion.c's file
 * header comment on why that's double-frequency relative to a full L+R
 * gait cycle, matching real bipedal vertical bob). Treat as starting
 * values for playtesting, not final. --- */
#define CAMERA_MOTION_WALK_VERTICAL_AMPLITUDE_METERS      0.008f
#define CAMERA_MOTION_WALK_LATERAL_AMPLITUDE_METERS       0.005f
#define CAMERA_MOTION_RUN_VERTICAL_AMPLITUDE_METERS       0.016f
#define CAMERA_MOTION_RUN_LATERAL_AMPLITUDE_METERS        0.010f
#define CAMERA_MOTION_SPRINT_VERTICAL_AMPLITUDE_METERS    0.025f
#define CAMERA_MOTION_SPRINT_LATERAL_AMPLITUDE_METERS     0.016f
#define CAMERA_MOTION_CROUCH_VERTICAL_AMPLITUDE_METERS    0.006f
#define CAMERA_MOTION_CROUCH_LATERAL_AMPLITUDE_METERS     0.004f

/* How fast bob fades in/out when motion_valid flips (airborne, idle,
 * wall-blocked, landing-reset) - a rate, not an instant cutoff, per
 * "smooth blending during starts, stops, and gait transitions". */
#define CAMERA_MOTION_GAIT_ACTIVE_BLEND_RATE_PER_SEC      6.0f
/* How fast the blended amplitude retargets when the active gait itself
 * changes (e.g. Walk -> Sprint) - independent of the active-blend rate
 * above so a gait change mid-stride doesn't also look like a stop/start.
 * Deliberately small: these amplitudes are only ~0.01-0.03m apart, so a
 * "fast" rate in absolute m/s terms would still cover the whole gap in
 * a single tick and snap anyway - this is tuned to take a few hundred
 * ms even for the largest gap (Crouch -> Sprint, ~0.019m). */
#define CAMERA_MOTION_GAIT_AMPLITUDE_BLEND_RATE_PER_SEC   0.05f

/* --- Start/stop/turn inertia - a small lagging offset from REALIZED
 * (not requested) horizontal acceleration, expressed in the body's own
 * local frame (x=right, y=up, z=forward) so a lean reads the same
 * regardless of which way the player is facing. --- */
#define CAMERA_MOTION_INERTIA_GAIN_SECONDS                0.12f
#define CAMERA_MOTION_INERTIA_SPRING_RATE_PER_SEC         10.0f
#define CAMERA_MOTION_INERTIA_MAX_OFFSET_METERS           0.03f

/* --- Landing dip/recovery (closes M7F) - amplitude by severity,
 * clamped so even a DAMAGING-tier impact can't exceed the ceiling
 * below; recovery duration reuses spec 17's shared timing since it's
 * meant to read as "one coherent settle", not two competing timers. --- */
#define CAMERA_MOTION_LANDING_SOFT_AMPLITUDE_METERS       0.020f
#define CAMERA_MOTION_LANDING_STANDARD_AMPLITUDE_METERS   0.050f
#define CAMERA_MOTION_LANDING_HEAVY_AMPLITUDE_METERS      0.090f
#define CAMERA_MOTION_LANDING_DAMAGING_AMPLITUDE_METERS   0.120f
#define CAMERA_MOTION_LANDING_RECOVERY_SECONDS            0.35f

#endif /* OUTPOST_CAMERA_MOTION_TUNING_H */
