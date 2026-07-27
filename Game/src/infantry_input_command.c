#include "infantry_input_command.h"

#include "eisenfront/input.h"

void infantry_input_command_resolve_move_axes(bool forward_held, bool backward_held, bool right_held,
                                               bool left_held, float *out_axis_x, float *out_axis_y) {
    *out_axis_x = 0.0f;
    *out_axis_y = 0.0f;
    if (forward_held) {
        *out_axis_y += 1.0f;
    }
    if (backward_held) {
        *out_axis_y -= 1.0f;
    }
    if (right_held) {
        *out_axis_x += 1.0f;
    }
    if (left_held) {
        *out_axis_x -= 1.0f;
    }
}

InfantryInputCommand infantry_input_command_sample(float client_delta_seconds) {
    InfantryInputCommand command = {0};
    infantry_input_command_resolve_move_axes(input_key_held(KEY_W), input_key_held(KEY_S),
                                              input_key_held(KEY_D), input_key_held(KEY_A),
                                              &command.move_axis_x, &command.move_axis_y);
    input_mouse_delta(&command.look_delta_yaw, &command.look_delta_pitch);
    command.sprint_held = input_key_held(KEY_LSHIFT);
    command.walk_modifier_held = input_key_held(KEY_LCTRL);
    command.crouch_held = input_key_held(KEY_C);
    command.jump_requested = input_key_pressed(KEY_SPACE);
    command.client_delta_seconds = client_delta_seconds;
    return command;
}
