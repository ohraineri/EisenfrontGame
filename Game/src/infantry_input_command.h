/*
 * One sampled frame of player intent, consumed by the fixed-timestep
 * simulation loop (see sim_clock.h). Sampled once per render frame,
 * not per sim tick - held-key state doesn't change between
 * input_new_frame() calls, so reusing one command across however many
 * fixed ticks a render frame produces is correct. jump_requested is a
 * one-shot edge: the simulation loop must clear it after the first
 * tick consumes it, so a multi-tick frame can't queue multiple jumps.
 *
 * Field shape follows the Infantry Controller spec's
 * FInfantryInputCommand (sequence number and network fields omitted -
 * no netcode exists yet; added additively when that milestone lands).
 */
#ifndef OUTPOST_INFANTRY_INPUT_COMMAND_H
#define OUTPOST_INFANTRY_INPUT_COMMAND_H

#include <stdbool.h>

typedef struct InfantryInputCommand {
    float move_axis_x;          /* -1 (left) .. +1 (right), body-relative */
    float move_axis_y;          /* -1 (back) .. +1 (forward), body-relative */
    float look_delta_yaw;
    float look_delta_pitch;
    bool  sprint_held;
    /* Precision/quiet gait modifier (spec 5's WalkModifier action):
     * base gait with no modifiers held is Run; holding this drops to
     * Walk. Sprint still takes priority over this when eligible. */
    bool  walk_modifier_held;
    bool  crouch_held;
    bool  jump_requested;       /* one-shot; caller clears after the first tick consumes it */
    float client_delta_seconds; /* real render-frame delta, for reference only - simulation itself uses the fixed step */
} InfantryInputCommand;

/* Pure - resolves opposing keys on the same axis to zero. Testable
 * without a live Input subsystem. */
void infantry_input_command_resolve_move_axes(bool forward_held, bool backward_held, bool right_held,
                                               bool left_held, float *out_axis_x, float *out_axis_y);

/* Samples raw keyboard/mouse state into a command. Call once per
 * render frame - see the struct comment on why. */
InfantryInputCommand infantry_input_command_sample(float client_delta_seconds);

#endif /* OUTPOST_INFANTRY_INPUT_COMMAND_H */
