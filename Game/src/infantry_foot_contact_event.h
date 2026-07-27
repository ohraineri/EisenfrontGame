/*
 * InfantryFootContactEvent - the gameplay-neutral foot-contact contract
 * (Infantry Controller spec 25, M10). Deliberately its own small header,
 * included by both infantry_entity.h (the producer) and game_audio.h
 * (a consumer), rather than folded into either - so a future consumer
 * (animation, AI perception, particles, decals, networking, equipment
 * audio) only needs this one data-only header, not infantry_entity.h's
 * full ECS/physics/camera dependency chain.
 *
 * Ownership model (see infantry_entity.h's
 * infantry_entity_pop_foot_contact_event() for the enforcement point):
 *
 *   Locomotion producer -> bounded queue -> single dispatcher -> zero or more consumers
 *
 * Exactly one dispatcher (main.c in this vertical slice) may drain the
 * queue; it then fans the same immutable event out to every registered
 * consumer itself. Consumers never call the pop API directly - the
 * first one to do so would remove the event for everybody else.
 */
#ifndef OUTPOST_INFANTRY_FOOT_CONTACT_EVENT_H
#define OUTPOST_INFANTRY_FOOT_CONTACT_EVENT_H

#include "player_movement.h"
#include "surface_type.h"

#include <cglm/cglm.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct InfantryFootContactEvent {
    PlayerFoot  foot;
    vec3        world_position; /* resolved at the actual contact point, not a stale/camera position */
    SurfaceType surface_type;   /* resolved only at event creation, never polled every tick */
    PlayerGait  gait;
    float       movement_speed; /* planar speed at contact, meters/sec */
    float       intensity;      /* normalized 0..1, from player_movement_footstep_intensity_for_gait() */
    uint64_t    sim_tick_index; /* the fixed tick this contact occurred on - see sim_clock_tick_index() */
    /* Always true by construction (events are only ever produced from
     * real grounded contact) - a richer ground-contact body/material
     * reference is deferred to the M9D-migration item, since
     * CharacterController doesn't expose one yet. */
    bool grounded;
} InfantryFootContactEvent;

#endif /* OUTPOST_INFANTRY_FOOT_CONTACT_EVENT_H */
