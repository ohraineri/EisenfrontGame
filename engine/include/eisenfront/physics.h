/*
 * Physics - an in-house rigid body simulation: sphere/box (and, for the
 * character controller, capsule) shapes, collision layers, triggers,
 * raycasts, and a simple upright capsule character controller. Written
 * entirely in this engine's own ISO C23 (see the file header comment on
 * why: CLAUDE.md's third-party table does not name a physics library
 * the way it names miniaudio for Audio or ENet for Networking, and
 * every real off-the-shelf 3D physics engine - Jolt, Bullet, ODE - is a
 * C++ codebase with at best a thin C wrapper, which would mean mixing a
 * C++ toolchain into an otherwise pure-C23 build for a dependency this
 * project was never told to use. Writing it directly in C23 sidesteps
 * that entirely and keeps physics testable the same way every other
 * module in this engine already is).
 *
 * Scope, stated plainly rather than left implicit:
 *   - Linear dynamics only. Bodies do not rotate in response to
 *     collisions - no angular velocity, torque or inertia tensor is
 *     simulated. Rotation is stored on a body and settable
 *     (rigid_body_set_transform) but the simulation never changes it on
 *     its own. A full engine's rigid body solver would; this one
 *     doesn't, and says so instead of quietly producing bodies that
 *     never tumble.
 *   - Exact narrow-phase collision for sphere-sphere, sphere-box and
 *     box-box (box shapes are treated as world-axis-aligned bounding
 *     boxes for collision purposes regardless of a body's rotation -
 *     i.e. this is AABB-vs-AABB, not OBB-vs-OBB).
 *   - No broadphase acceleration structure: every step tests every
 *     active body pair (O(n^2)). Correct at the body counts a single
 *     game level typically needs; a production engine chasing thousands
 *     of simultaneously active bodies would add a spatial structure
 *     (grid/BVH) in front of this narrow phase without changing it.
 *   - The character controller's capsule is always upright (its axis is
 *     the world Y axis) and only collides against the world (sphere and
 *     box bodies), not against other character controllers.
 *
 * Dependencies: Core, cglm.
 */
#ifndef EISENFRONT_PHYSICS_H
#define EISENFRONT_PHYSICS_H

#include "core.h"

#include <cglm/cglm.h>

/* ==================================================================== *
 * Shapes
 * ==================================================================== */

typedef enum ShapeType {
    SHAPE_TYPE_SPHERE = 0,
    SHAPE_TYPE_BOX,
    SHAPE_TYPE_CAPSULE, /* rigid bodies may use one, but see the file header comment: */
                        /* rigid-body-vs-rigid-body narrow phase only covers sphere/box exactly - */
                        /* a capsule rigid body collides via a bounding-sphere approximation. */
} ShapeType;

typedef struct Shape {
    ShapeType type;
    float     radius;      /* SPHERE, CAPSULE */
    float     half_height; /* CAPSULE: half the height of the cylindrical section, excluding the end caps */
    vec3      half_extents; /* BOX */
} Shape;

ENGINE_API Shape shape_sphere(float radius);
ENGINE_API Shape shape_box(vec3 half_extents);
ENGINE_API Shape shape_capsule(float radius, float half_height);

/* ==================================================================== *
 * Collision layers - up to 32 independent layers, bitmask filtering.
 * Two bodies are tested against each other only if each one's layer is
 * present in the other's layer_mask (a symmetric check).
 * ==================================================================== */

typedef uint32_t CollisionLayer;
#define COLLISION_LAYER_ALL ((CollisionLayer)0xFFFFFFFFu)
#define COLLISION_LAYER_NONE ((CollisionLayer)0u)

/* ==================================================================== *
 * Rigid bodies
 * ==================================================================== */

typedef enum BodyType {
    BODY_TYPE_STATIC = 0, /* infinite mass, never integrated, still collides */
    BODY_TYPE_KINEMATIC,  /* infinite mass, moved by the game (position and/or velocity), pushes dynamics but is never pushed back */
    BODY_TYPE_DYNAMIC,    /* full simulation: gravity, forces, collision response */
} BodyType;

typedef struct RigidBodyDesc {
    BodyType       type;
    Shape          shape;
    vec3           position;
    versor         rotation;
    float          mass;            /* ignored for STATIC/KINEMATIC */
    float          restitution;     /* bounciness, 0..1 */
    float          friction;        /* 0 and up; combined between a pair as sqrt(a*b) */
    float          linear_damping;  /* 0..1 per second, fraction of velocity retained */
    CollisionLayer layer;
    CollisionLayer layer_mask;
    bool           is_trigger; /* reports overlap via the trigger callback; never resolved physically */
} RigidBodyDesc;

ENGINE_API RigidBodyDesc rigid_body_desc_default(void);

typedef uint32_t BodyId;
#define PHYSICS_INVALID_BODY_ID ((BodyId)0)

typedef struct PhysicsWorldDesc {
    uint32_t max_bodies;
    vec3     gravity;
} PhysicsWorldDesc;

ENGINE_API PhysicsWorldDesc physics_world_desc_default(void);

typedef struct PhysicsWorld PhysicsWorld;

ENGINE_API Result physics_world_create(const PhysicsWorldDesc *desc, PhysicsWorld **out_world);
ENGINE_API void   physics_world_destroy(PhysicsWorld *world);

/* Advances the simulation by delta_time: integrates forces and
 * velocities, then detects and resolves collisions for every active
 * body pair (see the file header comment on the O(n^2) all-pairs
 * scope), then fires trigger begin/end events for pairs where at least
 * one body is a trigger. Call once per frame (or on a fixed sub-step
 * schedule the game manages itself - this module does not impose one). */
ENGINE_API void physics_world_step(PhysicsWorld *world, float delta_time);

ENGINE_API Result rigid_body_create(PhysicsWorld *world, const RigidBodyDesc *desc,
                                     BodyId *out_body);
ENGINE_API Result rigid_body_destroy(PhysicsWorld *world, BodyId body);
ENGINE_API bool   rigid_body_is_valid(const PhysicsWorld *world, BodyId body);

ENGINE_API Result rigid_body_get_position(const PhysicsWorld *world, BodyId body,
                                           vec3 out_position);
ENGINE_API Result rigid_body_get_rotation(const PhysicsWorld *world, BodyId body,
                                           versor out_rotation);
/* Teleports the body - the normal way to drive a KINEMATIC body's
 * position directly (as opposed to giving it a velocity and letting
 * physics_world_step() integrate it). */
ENGINE_API Result rigid_body_set_transform(PhysicsWorld *world, BodyId body, vec3 position,
                                            versor rotation);

ENGINE_API Result rigid_body_get_linear_velocity(const PhysicsWorld *world, BodyId body,
                                                  vec3 out_velocity);
ENGINE_API Result rigid_body_set_linear_velocity(PhysicsWorld *world, BodyId body, vec3 velocity);
/* Accumulated force, applied over the next physics_world_step() call
 * and then cleared - call every step you want it to keep acting
 * (exactly like a continuous force such as gravity or thrust). */
ENGINE_API Result rigid_body_apply_force(PhysicsWorld *world, BodyId body, vec3 force);
/* Instantaneous velocity change (impulse = force * time collapsed into
 * one step) - for one-off effects like a jump or an explosion. */
ENGINE_API Result rigid_body_apply_impulse(PhysicsWorld *world, BodyId body, vec3 impulse);

/* began == true the first step two bodies (at least one a trigger)
 * overlap, false the step they stop overlapping. Fires at most once per
 * transition, from inside physics_world_step(). */
typedef void (*TriggerEventFn)(BodyId trigger, BodyId other, bool began, void *userdata);
ENGINE_API void physics_world_set_trigger_callback(PhysicsWorld *world, TriggerEventFn fn,
                                                     void *userdata);

/* ==================================================================== *
 * Raycasts
 * ==================================================================== */

typedef struct RaycastHit {
    BodyId body;
    float  distance;
    vec3   point;
    vec3   normal;
} RaycastHit;

/* direction must be normalized. Finds the closest hit (not just the
 * first tested), among bodies whose layer is present in layer_mask. */
ENGINE_API bool physics_raycast(const PhysicsWorld *world, vec3 origin, vec3 direction,
                                 float max_distance, CollisionLayer layer_mask,
                                 RaycastHit *out_hit);

/* ==================================================================== *
 * Character controller - an always-upright capsule moved by
 * collide-and-slide against the world's sphere/box bodies (see the file
 * header comment on scope).
 * ==================================================================== */

typedef struct CharacterControllerDesc {
    float          radius;
    float          half_height;
    vec3           position;
    CollisionLayer layer;
    CollisionLayer layer_mask;
} CharacterControllerDesc;

typedef struct CharacterController CharacterController;

/* world must outlive the controller; the controller does not own it. */
ENGINE_API Result character_controller_create(PhysicsWorld                  *world,
                                               const CharacterControllerDesc *desc,
                                               CharacterController          **out_controller);
ENGINE_API void   character_controller_destroy(CharacterController *controller);

/* Attempts to move by desired_displacement, sliding along anything it
 * would otherwise penetrate rather than stopping dead; the resulting
 * position (which may differ from position + desired_displacement) is
 * written to out_position. delta_time drives the grounded probe's
 * hysteresis - see character_controller_is_grounded(). */
ENGINE_API void character_controller_move(CharacterController *controller,
                                           vec3 desired_displacement, float delta_time,
                                           vec3 out_position);
ENGINE_API void character_controller_get_position(const CharacterController *controller,
                                                    vec3 out_position);
/* Teleports the controller directly - no collide-and-slide, no sweep
 * against the world along the way, exactly like rigid_body_set_transform()
 * for a rigid body. Does not itself change the grounded state;
 * character_controller_is_grounded() still reports whatever the most
 * recent character_controller_move() found until the next one runs. */
ENGINE_API void character_controller_set_position(CharacterController *controller, vec3 position);
/* True if a short downward probe from the capsule's bottom found
 * something within a small tolerance - call after
 * character_controller_move() for the current step's answer.
 * Hysteresis: gaining contact takes effect the instant a probe hits,
 * but losing it only takes effect after continuous misses exceed a
 * small internal tolerance, so a brief gap or mesh seam in the floor
 * doesn't flicker the grounded state every other tick. */
ENGINE_API bool character_controller_is_grounded(const CharacterController *controller);
/* Surface normal from the most recent grounded contact (or (0,1,0) if
 * never grounded) - held across the same hysteresis window as
 * character_controller_is_grounded(), so it doesn't need re-fetching
 * every tick just because a single probe momentarily missed.
 * Box bodies (this engine's only floor geometry today) are always
 * world-axis-aligned - see this file's header comment - so a
 * straight-down probe against one is reported as (0,1,0) directly;
 * there is no per-face normal tracking because there is no way to
 * construct a sloped/ramp box in this physics module yet. Sphere/
 * capsule bodies get an exact computed normal. True inclined-surface
 * support needs an oriented or ramp shape type added to Physics first
 * - this query is the seam Game-side slope logic (Infantry Controller
 * spec 14) is meant to consume once that exists. */
ENGINE_API void character_controller_get_ground_normal(const CharacterController *controller,
                                                         vec3 out_normal);

/* Resizes the capsule, keeping its bottom contact point fixed - the
 * center moves up when growing, down when shrinking - the natural
 * behavior for a crouch/stand transition (Infantry Controller spec
 * Part IX). Shrinking is always accepted (a smaller capsule can only
 * reduce overlap, never create new penetration). Growing is checked
 * against the world first at the new size; if it would overlap
 * anything, nothing changes and false is returned so the caller stays
 * in its current (smaller) size - the standard "blocked by a low
 * ceiling" case. Only ever changes radius/half_height and position;
 * never the layer or layer_mask. */
ENGINE_API bool character_controller_resize(CharacterController *controller, float new_radius,
                                             float new_half_height);

#endif /* EISENFRONT_PHYSICS_H */
