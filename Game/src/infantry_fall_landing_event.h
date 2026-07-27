/*
 * InfantryFallLandingEvent - stub sink for spec 18.1's FFallLandingEvent
 * (populated on every landing - see resolve_vertical_state() in
 * infantry_entity.c). Its own small header, same rationale as
 * infantry_foot_contact_event.h: M11's camera_motion.c consumes this
 * without needing infantry_entity.h's full ECS/physics/camera include
 * chain. PlayerId, EquipmentMass and WasTraversalInitiated from the
 * spec struct are omitted (single entity, no equipment system, no
 * traversal system).
 *
 * Dispatched two ways from InfantryLocomotionComponent: a sticky
 * last_landing_event/has_landing_event pair (debug_overlay.c's "last
 * known landing" readout) and a bounded pop queue
 * (infantry_entity_pop_landing_event(), the single-dispatcher path
 * M11's landing camera response drains) - see infantry_entity.h's
 * InfantryLandingEventQueue.
 */
#ifndef OUTPOST_INFANTRY_FALL_LANDING_EVENT_H
#define OUTPOST_INFANTRY_FALL_LANDING_EVENT_H

#include "player_movement.h"

#include <cglm/cglm.h>
#include <stdbool.h>

typedef struct InfantryFallLandingEvent {
    float                 fall_start_height;
    float                 landing_height;
    float                 max_downward_speed;
    float                 impact_speed;
    float                 air_time_seconds;
    vec3                  surface_normal;
    bool                  was_jump_initiated;
    PlayerLandingSeverity severity;
} InfantryFallLandingEvent;

#endif /* OUTPOST_INFANTRY_FALL_LANDING_EVENT_H */
