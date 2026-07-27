#include "player.h"

#include "eisenfront/input.h"

Player player_create(vec3 start_position, float aspect_ratio) {
    Player player;
    player.camera =
        camera_create_perspective(start_position, glm_rad(70.0f), aspect_ratio, 0.05f, 500.0f);
    player.move_speed_units_per_sec = 5.0f;
    player.look_sensitivity_radians_per_pixel = 0.0025f;
    return player;
}

void player_update_freecam(Player *player, float delta_seconds) {
    float dx = 0.0f, dy = 0.0f;
    input_mouse_delta(&dx, &dy);
    camera_rotate(&player->camera, dx * player->look_sensitivity_radians_per_pixel,
                   -dy * player->look_sensitivity_radians_per_pixel);

    vec3        offset = {0.0f, 0.0f, 0.0f};
    const float speed = player->move_speed_units_per_sec *
                         (input_key_held(KEY_LSHIFT) ? 2.5f : 1.0f) * delta_seconds;

    if (input_key_held(KEY_W)) {
        offset[2] += speed;
    }
    if (input_key_held(KEY_S)) {
        offset[2] -= speed;
    }
    if (input_key_held(KEY_D)) {
        offset[0] += speed;
    }
    if (input_key_held(KEY_A)) {
        offset[0] -= speed;
    }
    if (input_key_held(KEY_SPACE)) {
        offset[1] += speed;
    }
    if (input_key_held(KEY_LCTRL)) {
        offset[1] -= speed;
    }

    camera_move_local(&player->camera, offset);
}
