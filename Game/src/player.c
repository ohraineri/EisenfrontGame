#include "player.h"

#include "eisenfront/input.h"

#define PLAYER_GRAVITY_UNITS_PER_SEC2 -20.0f
#define PLAYER_JUMP_SPEED_UNITS_PER_SEC 6.5f
#define PLAYER_GROUNDED_STICK_VELOCITY -0.5f

Result player_create(PhysicsWorld *world, vec3 start_position, float aspect_ratio,
                      Player *out_player) {
    if (world == nullptr || out_player == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const CharacterControllerDesc controller_desc = {
        .radius = 0.35f,
        .half_height = 0.55f,
        .position = {start_position[0], start_position[1], start_position[2]},
        .layer = 1u,
        .layer_mask = COLLISION_LAYER_ALL,
    };
    CharacterController *controller = nullptr;
    const Result          result = character_controller_create(world, &controller_desc, &controller);
    if (result != RESULT_OK) {
        return result;
    }

    /* Eye height above the capsule center: half_height + radius puts
     * it at the very top of the head, minus a little so the eye sits
     * just under the crown rather than exactly on the boundary. */
    out_player->eye_height_offset = controller_desc.half_height + controller_desc.radius - 0.1f;

    vec3 eye_position = {start_position[0], start_position[1] + out_player->eye_height_offset,
                          start_position[2]};
    out_player->camera = camera_create_perspective(eye_position, glm_rad(70.0f), aspect_ratio, 0.05f, 500.0f);
    out_player->controller = controller;
    out_player->move_speed_units_per_sec = 5.0f;
    out_player->look_sensitivity_radians_per_pixel = 0.0025f;
    out_player->vertical_velocity = 0.0f;
    return RESULT_OK;
}

void player_destroy(Player *player) {
    if (player == nullptr) {
        return;
    }
    character_controller_destroy(player->controller);
}

void player_update(Player *player, float delta_seconds) {
    float dx = 0.0f, dy = 0.0f;
    input_mouse_delta(&dx, &dy);
    camera_rotate(&player->camera, dx * player->look_sensitivity_radians_per_pixel,
                   -dy * player->look_sensitivity_radians_per_pixel);

    vec3 forward, right;
    camera_get_forward(&player->camera, forward);
    camera_get_right(&player->camera, right);
    /* Ground movement only - pitch shouldn't tip the player into
     * walking into the floor/sky when looking up or down. */
    forward[1] = 0.0f;
    right[1] = 0.0f;
    glm_vec3_normalize(forward);
    glm_vec3_normalize(right);

    const float speed =
        player->move_speed_units_per_sec * (input_key_held(KEY_LSHIFT) ? 1.6f : 1.0f) * delta_seconds;
    vec3 horizontal_move = {0.0f, 0.0f, 0.0f};
    if (input_key_held(KEY_W)) {
        glm_vec3_muladds(forward, speed, horizontal_move);
    }
    if (input_key_held(KEY_S)) {
        glm_vec3_muladds(forward, -speed, horizontal_move);
    }
    if (input_key_held(KEY_D)) {
        glm_vec3_muladds(right, speed, horizontal_move);
    }
    if (input_key_held(KEY_A)) {
        glm_vec3_muladds(right, -speed, horizontal_move);
    }

    const bool grounded = character_controller_is_grounded(player->controller);
    if (grounded) {
        if (input_key_pressed(KEY_SPACE)) {
            player->vertical_velocity = PLAYER_JUMP_SPEED_UNITS_PER_SEC;
        } else if (player->vertical_velocity < 0.0f) {
            /* Small downward stick rather than 0: keeps the grounded
             * probe (a short ray - see physics.c) consistently finding
             * the floor every frame instead of flickering ungrounded
             * the instant vertical velocity hits exactly zero. */
            player->vertical_velocity = PLAYER_GROUNDED_STICK_VELOCITY;
        }
    }
    player->vertical_velocity += PLAYER_GRAVITY_UNITS_PER_SEC2 * delta_seconds;

    vec3 desired_displacement = {horizontal_move[0], player->vertical_velocity * delta_seconds,
                                  horizontal_move[2]};
    vec3 resulting_position;
    character_controller_move(player->controller, desired_displacement, resulting_position);

    player->camera.position[0] = resulting_position[0];
    player->camera.position[1] = resulting_position[1] + player->eye_height_offset;
    player->camera.position[2] = resulting_position[2];
}
