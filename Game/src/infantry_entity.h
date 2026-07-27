/*
 * InfantryEntity - the player's body, as a real ECS entity rather than
 * a plain struct (Infantry Controller spec Part II 4.1: infantry must
 * be composed of independently-typed components, reusable later by
 * other infantry - AI, vehicle crew, etc). Each InfantryEntity owns a
 * small private World of its own (max_entities=1), the same pattern
 * ai_soldier.c already uses for its squad - a single shared World
 * across player and AI entities is a later migration, not this one.
 *
 * Camera position is derived every frame from InfantryLocomotionComponent's
 * controller position plus a fixed eye-height offset, never set
 * directly - the controller is the single source of truth for where
 * the body physically is (unchanged from the pre-ECS Player design).
 */
#ifndef OUTPOST_INFANTRY_ENTITY_H
#define OUTPOST_INFANTRY_ENTITY_H

#include "eisenfront/camera.h"
#include "eisenfront/ecs.h"
#include "eisenfront/physics.h"

#include "infantry_fall_landing_event.h"
#include "infantry_foot_contact_event.h"
#include "infantry_input_command.h"
#include "player_movement.h"

#include <stdint.h>

/* Authoritative body position - capsule center, not eye height - and
 * body facing, independent of view/camera yaw (Infantry Controller
 * spec 4.4/11: the body controls horizontal displacement, the camera
 * controls view intent; looking does not rotate the collision body).
 * See player_movement_resolve_body_yaw() for how body_yaw_radians is
 * updated each tick. */
typedef struct InfantryTransformComponent {
    vec3  position;
    float body_yaw_radians;
} InfantryTransformComponent;

#define INFANTRY_FOOT_CONTACT_EVENT_CAPACITY 8u

/* Locomotion-owned gait/stride state (M10): the stride accumulator and
 * alternating-foot phase (see player_movement.h's PlayerGaitStrideState),
 * a bounded FIFO of produced-but-not-yet-dispatched events, and two
 * distinct diagnostic drop counters - dropped_event_count (queue
 * overflow: the dispatcher didn't drain in time) and
 * dropped_contact_count (a single tick's realized displacement exceeded
 * PLAYER_MAX_FOOT_CONTACTS_PER_TICK; see
 * player_movement_gait_stride_advance()'s drop policy). */
typedef struct InfantryGaitState {
    PlayerGaitStrideState    stride;
    float                    idle_seconds;
    bool                     was_noclip_enabled;
    InfantryFootContactEvent queue[INFANTRY_FOOT_CONTACT_EVENT_CAPACITY];
    uint32_t                 queue_head;
    uint32_t                 queue_count;
    uint32_t                 dropped_event_count;
    uint32_t                 dropped_contact_count;

    /* M11 presentation snapshot backing fields - last-resolved gait
     * (a per-tick local in update_grounded() otherwise, persisted here
     * so infantry_entity_get_gait_presentation_snapshot() has something
     * to read outside that call), whether THIS tick's stride state is
     * meaningful for bob presentation (false while airborne/idle/on a
     * landing-reset tick), and a generation counter bumped by
     * infantry_entity_reset_gait() (and once at creation) so presentation
     * consumers can detect a discontinuity (teleport, noclip toggle,
     * spawn) instead of interpolating across it. */
    PlayerGait               current_gait;
    bool                     motion_valid;
    uint32_t                 reset_generation;
} InfantryGaitState;

#define INFANTRY_LANDING_EVENT_QUEUE_CAPACITY 4u

/* Same bounded-FIFO/single-dispatcher shape as InfantryGaitState's
 * foot-contact queue, for InfantryFallLandingEvent (M11's landing camera
 * response is the first consumer). Kept separate from the sticky
 * last_landing_event/has_landing_event fields below, which remain a
 * different, simpler "what was the last landing" readout for
 * debug_overlay.c - not a dispatch queue. */
typedef struct InfantryLandingEventQueue {
    InfantryFallLandingEvent queue[INFANTRY_LANDING_EVENT_QUEUE_CAPACITY];
    uint32_t                 queue_head;
    uint32_t                 queue_count;
    uint32_t                 dropped_event_count;
} InfantryLandingEventQueue;

typedef struct InfantryLocomotionComponent {
    CharacterController *controller; /* owned by this entity */
    float                 eye_height_offset; /* above the controller's capsule center */
    float                 noclip_base_speed_units_per_sec; /* debug-only fly speed baseline - see PLAYER_NOCLIP_BASE_SPEED_UNITS_PER_SEC */
    float                 vertical_velocity;
    vec3                  horizontal_velocity; /* current XZ world-space velocity, Y always 0 */
    float                 stamina;             /* 0..PLAYER_STAMINA_MAX; gates sprint */
    bool                  sprint_locked_out;   /* latched at 0 stamina until regen clears it */
    bool                  is_sprinting;        /* this tick's resolved sprint state */
    /* Seconds since the wish direction last fell within the sprint
     * cone (spec 7.1) - sprint ends once this exceeds
     * PLAYER_SPRINT_CONE_GRACE_SECONDS. */
    float                 sprint_cone_exit_seconds;
    /* The last tier actually moved at (has_move_input was true) - used
     * to pick the right braking constant while decelerating after
     * input is released (spec 9's "state before release"), since tier
     * resolution trivially falls back to Run/Walk once has_move_input
     * is false. */
    PlayerSpeedTier       last_moving_tier;
    /* Confirmed stance and its anti-spam cooldown (spec Part IX). The
     * collision capsule always matches `stance` exactly (resized
     * atomically the instant a change succeeds); eye_height_offset
     * blends toward the stance's target over
     * PLAYER_STANCE_TRANSITION_SECONDS instead of snapping - the
     * "continuous visually but collision-safe discretely" split spec
     * 20 asks for. */
    PlayerStance          stance;
    float                 stance_change_cooldown_seconds;

    /* Jump/fall/land (spec Part VIII). */
    PlayerVerticalState   vertical_state;
    float                 jump_lockout_seconds_remaining;    /* repeat-jump lockout, set at landing */
    float                 landing_recovery_seconds_remaining;
    /* >0 while a crouched jump request is waiting on a stand-up resize
     * to settle before the jump itself is allowed to fire (crouch/jump
     * interaction point 2/4/5 - see resolve_jump_request() for why
     * this can never fire in the same tick the resize succeeds). */
    float                 stand_to_jump_pending_seconds;
    float                 air_time_seconds;
    float                 fall_start_height;
    float                 max_downward_speed;
    bool                  jump_initiated_current_airborne_phase;
    InfantryFallLandingEvent last_landing_event;
    bool                  has_landing_event; /* whether last_landing_event holds real data yet */

    InfantryGaitState         gait;
    InfantryLandingEventQueue landing_queue;
} InfantryLocomotionComponent;

typedef struct InfantryViewComponent {
    Camera camera;
    float  look_sensitivity_radians_per_pixel;
    /* Debug-overlay-only (Phase 8): flies through geometry ignoring
     * collision entirely instead of colliding-and-sliding. The
     * controller's own position is still kept in sync every tick (via
     * character_controller_set_position()) so turning noclip back off
     * doesn't snap the player back to wherever they last had real
     * collision. */
    bool   noclip_enabled;
} InfantryViewComponent;

typedef struct InfantryEntity {
    World          *world; /* owned; private to this entity - see file header comment */
    Entity          entity;
    ComponentTypeId transform_type;
    ComponentTypeId locomotion_type;
    ComponentTypeId view_type;
} InfantryEntity;

/* start_position is where the capsule's CENTER spawns, not the eye -
 * see InfantryLocomotionComponent.eye_height_offset. physics_world
 * must outlive this entity. */
Result infantry_entity_create(PhysicsWorld *physics_world, vec3 start_position, float aspect_ratio,
                               InfantryEntity *out_entity);
void   infantry_entity_destroy(InfantryEntity *entity);

/* Pointers are valid for the entity's lifetime (a single-entity world
 * never removes/reshuffles components) - see ecs.h's file header
 * comment on when such a pointer would otherwise be invalidated. */
InfantryTransformComponent  *infantry_entity_get_transform(const InfantryEntity *entity);
InfantryLocomotionComponent *infantry_entity_get_locomotion(const InfantryEntity *entity);
InfantryViewComponent       *infantry_entity_get_view(const InfantryEntity *entity);

/* Mouse-look (relative mode must already be enabled on the window).
 * Call once per render frame, regardless of how many fixed simulation
 * ticks that frame produces (see sim_clock.h) - applying the same
 * sampled look delta more than once per frame would over-rotate. */
void infantry_entity_apply_look(InfantryEntity *entity, const InfantryInputCommand *command);

/* Resolves the SurfaceType at world_position - called only at the
 * moment a foot-contact event is actually being created (never every
 * fixed tick), so the raycast/lookup cost only exists when it's about
 * to be used. userdata is whatever the caller bound it to (e.g. Outpost's
 * PhysicsWorld + OutpostLevel + ground BodyId). */
typedef SurfaceType (*InfantryGroundSurfaceLookupFn)(void *userdata, vec3 world_position);

/* One fixed-timestep simulation tick: WASD ground movement + sprint/
 * stamina + jump, collide-and-slide against the physics world via the
 * CharacterController, plus gait/foot-contact-event production (M10).
 * Call once per fixed sim tick (zero or more per render frame);
 * command->jump_requested must be cleared by the caller after the first
 * tick that consumes it, so a multi-tick frame doesn't queue multiple
 * jumps. sim_tick_index should come from sim_clock_tick_index() -
 * monotonic, advancing once per fixed tick, stamped onto any event
 * produced this call. surface_lookup_fn/surface_lookup_userdata may be
 * nullptr (events then default to SURFACE_TYPE_SOIL) but Outpost always
 * supplies a real one. */
void infantry_entity_update_fixed(InfantryEntity *entity, const InfantryInputCommand *command,
                                   float fixed_delta_seconds, uint64_t sim_tick_index,
                                   InfantryGroundSurfaceLookupFn surface_lookup_fn,
                                   void *surface_lookup_userdata);

/* Single-owner drain - see infantry_foot_contact_event.h's file header
 * comment for the full ownership model. Only ONE dispatcher may call
 * this (main.c in this vertical slice); it must fan the popped event
 * out to every consumer itself rather than let each consumer pop
 * independently, since the first pop removes the event for everyone
 * else. Dequeues oldest-first; returns false once the queue is empty.
 * If more events are produced than INFANTRY_FOOT_CONTACT_EVENT_CAPACITY
 * can hold before being drained, the OLDEST unconsumed event is dropped
 * (see InfantryGaitState.dropped_event_count) - production is never
 * blocked waiting for the dispatcher. */
bool infantry_entity_pop_foot_contact_event(InfantryEntity *entity, InfantryFootContactEvent *out_event);

/* Resets gait phase to the documented initial foot (LEFT) and zeroes
 * accumulated stride distance, without touching the event queue or
 * either drop counter. Bumps InfantryGaitState.reset_generation, the
 * signal presentation consumers (camera_motion.c) use to detect a
 * discontinuity. Every non-ordinary-locomotion position change must
 * call this so a stale partial stride from before the change can never
 * surface as a phantom footstep afterward - entity creation (handled
 * internally), any teleport/scripted-placement call site
 * (debug_overlay.c's "Teleport to look direction", main.c's smoke-test
 * waypoint branch), and toggling noclip on or off (handled internally
 * by infantry_entity_update_fixed, which detects the transition). */
void infantry_entity_reset_gait(InfantryEntity *entity);

/* Read-only view of the CURRENT gait/stride state for presentation
 * consumers (M11's camera_motion.c) - see PlayerGaitPresentationSnapshot's
 * own doc comment in player_movement.h for the "no second odometer"
 * contract this exists to support. */
void infantry_entity_get_gait_presentation_snapshot(const InfantryEntity *entity,
                                                      PlayerGaitPresentationSnapshot *out_snapshot);

/* Single-owner drain for InfantryFallLandingEvent - same ownership model
 * as infantry_entity_pop_foot_contact_event() (only ONE dispatcher may
 * call this; it fans the event out to consumers itself). Unlike the
 * foot-contact queue (drained once per render frame by convention), this
 * is meant to be drained once per FIXED TICK, immediately after
 * infantry_entity_update_fixed() - landing response (M11) must enter the
 * consumer's current-fixed-tick state on the same tick the landing was
 * detected, not a render frame later. */
bool infantry_entity_pop_landing_event(InfantryEntity *entity, InfantryFallLandingEvent *out_event);

#endif /* OUTPOST_INFANTRY_ENTITY_H */
