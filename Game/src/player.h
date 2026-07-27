/*
 * Player - camera + input + a real Physics CharacterController.
 * Replaces Phase 2's collision-free freecam: movement is now
 * collide-and-slide against every static body in the outpost (see
 * outpost_scene.c), with simple gravity/jump layered on top in Game
 * code, since CharacterController itself only does collide-and-slide
 * for whatever displacement it's given each call - it does not
 * integrate gravity on its own (unlike a BODY_TYPE_DYNAMIC rigid body;
 * see physics.h). The camera position is derived every frame from the
 * controller's capsule position plus a fixed eye-height offset, never
 * set directly - the controller is the single source of truth for
 * where the player physically is.
 */
#ifndef OUTPOST_PLAYER_H
#define OUTPOST_PLAYER_H

#include "eisenfront/camera.h"
#include "eisenfront/physics.h"
#include "eisenfront/window.h"

typedef struct Player {
    Camera                camera;
    CharacterController  *controller; /* owned */
    float                 move_speed_units_per_sec;
    float                 look_sensitivity_radians_per_pixel;
    float                 vertical_velocity;
    float                 eye_height_offset; /* above the controller's capsule center */
} Player;

/* start_position is where the capsule's CENTER spawns, not the eye -
 * see eye_height_offset. world must outlive the player. */
Result player_create(PhysicsWorld *world, vec3 start_position, float aspect_ratio,
                      Player *out_player);
void   player_destroy(Player *player);

/* Mouse-look (relative mode must already be enabled on the window) +
 * WASD ground movement + space to jump, collide-and-slide against the
 * physics world via the CharacterController. */
void player_update(Player *player, float delta_seconds);

#endif /* OUTPOST_PLAYER_H */
