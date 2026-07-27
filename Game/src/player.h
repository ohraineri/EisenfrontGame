/*
 * Player - camera + input for now (Phase 2: freecam, no collision).
 * Phase 5 replaces player_update_freecam() with a real
 * CharacterController-driven update without changing this struct's
 * shape (the camera stays the single source of truth for where the
 * player is looking/standing).
 */
#ifndef OUTPOST_PLAYER_H
#define OUTPOST_PLAYER_H

#include "eisenfront/camera.h"
#include "eisenfront/window.h"

typedef struct Player {
    Camera camera;
    float  move_speed_units_per_sec;
    float  look_sensitivity_radians_per_pixel;
} Player;

Player player_create(vec3 start_position, float aspect_ratio);

/* Mouse-look (relative mode must already be enabled on the window) +
 * WASD/space/ctrl fly movement, no collision. */
void player_update_freecam(Player *player, float delta_seconds);

#endif /* OUTPOST_PLAYER_H */
