/*
 * AI soldiers - the vertical slice's only real ECS gameplay layer.
 * Transform + Patrol components on a handful of entities, moved every
 * frame by a registered EcsSystemFn (a straight-line waypoint patrol,
 * not pathfinding - out of scope per the plan this was built from).
 * Each entity's Transform is mirrored onto a BODY_TYPE_KINEMATIC
 * Physics capsule body every frame so the player's CharacterController
 * collides against soldiers exactly like it does static geometry (see
 * physics.c: character_controller_move() tests every active
 * non-trigger body regardless of type).
 *
 * Visual representation is a plain box, not a capsule mesh - this
 * slice's whole aesthetic is boxy procedural placeholders (see
 * outpost_scene.c), and a real capsule mesh generator would be pure
 * visual polish with no bearing on what this phase is actually
 * proving: ECS driving real per-frame gameplay state, kept in sync
 * with a Physics capsule shape, rendered through the same per-object
 * uModel path static geometry doesn't need.
 */
#ifndef OUTPOST_AI_SOLDIER_H
#define OUTPOST_AI_SOLDIER_H

#include "eisenfront/ecs.h"
#include "eisenfront/material.h"
#include "eisenfront/mesh.h"
#include "eisenfront/physics.h"
#include "eisenfront/shader.h"

#define AI_SOLDIER_COUNT 4u
#define AI_SOLDIER_MAX_WAYPOINTS 4u

typedef struct AiTransform {
    vec3  position;
    float yaw_radians;
} AiTransform;

typedef struct AiPatrol {
    vec3     waypoints[AI_SOLDIER_MAX_WAYPOINTS];
    uint32_t waypoint_count;
    uint32_t current_index;
    float    speed_units_per_sec;
} AiPatrol;

typedef struct AiSoldierSquad {
    World          *ecs_world;
    ComponentTypeId transform_type;
    ComponentTypeId patrol_type;
    Entity          entities[AI_SOLDIER_COUNT];
    BodyId          bodies[AI_SOLDIER_COUNT];
    PhysicsWorld   *physics_world; /* not owned - stored for the patrol system's body sync */
    Mesh           *mesh;          /* one shared object-space box, one draw per soldier via uModel */
    Material       *material;
    uint32_t        count;
} AiSoldierSquad;

/* lit_shader is not retained - reused, matching every other Game
 * material. physics_world must outlive the squad. */
Result ai_soldier_squad_create(ShaderProgram *lit_shader, PhysicsWorld *physics_world,
                                AiSoldierSquad *out_squad);
void   ai_soldier_squad_destroy(AiSoldierSquad *squad);

/* Advances the patrol system by delta_seconds (runs the ECS world). */
void ai_soldier_squad_update(AiSoldierSquad *squad, float delta_seconds);

/* Builds soldier `index`'s current world transform for rendering. */
void ai_soldier_get_world_matrix(const AiSoldierSquad *squad, uint32_t index, mat4 out_matrix);

#endif /* OUTPOST_AI_SOLDIER_H */
