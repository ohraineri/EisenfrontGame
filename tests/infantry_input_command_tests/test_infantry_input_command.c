/*
 * Only infantry_input_command_resolve_move_axes is pure and exercised
 * here - infantry_input_command_sample() polls a live Input
 * subsystem and isn't meaningfully testable without a real window/
 * event loop (see input_tests for that side).
 */
#include "infantry_input_command.h"

#include "unity.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_no_keys_held_gives_zero_axes(void) {
    float axis_x = 99.0f, axis_y = 99.0f;
    infantry_input_command_resolve_move_axes(false, false, false, false, &axis_x, &axis_y);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, axis_x);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, axis_y);
}

static void test_single_directions_resolve_to_unit_axes(void) {
    float axis_x, axis_y;

    infantry_input_command_resolve_move_axes(true, false, false, false, &axis_x, &axis_y);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, axis_y);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, axis_x);

    infantry_input_command_resolve_move_axes(false, true, false, false, &axis_x, &axis_y);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, axis_y);

    infantry_input_command_resolve_move_axes(false, false, true, false, &axis_x, &axis_y);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, axis_x);

    infantry_input_command_resolve_move_axes(false, false, false, true, &axis_x, &axis_y);
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, axis_x);
}

static void test_opposing_inputs_on_same_axis_cancel_to_zero(void) {
    float axis_x, axis_y;

    infantry_input_command_resolve_move_axes(true, true, false, false, &axis_x, &axis_y);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, axis_y);

    infantry_input_command_resolve_move_axes(false, false, true, true, &axis_x, &axis_y);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, axis_x);
}

static void test_diagonal_axes_are_not_pre_normalized(void) {
    /* Diagonal magnitude clamping to 1.0 happens downstream (camera-
     * space wish-direction normalization in player.c) - at this layer
     * both axes are independently -1/0/+1, by design. */
    float axis_x, axis_y;
    infantry_input_command_resolve_move_axes(true, false, true, false, &axis_x, &axis_y);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, axis_x);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, axis_y);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_no_keys_held_gives_zero_axes);
    RUN_TEST(test_single_directions_resolve_to_unit_axes);
    RUN_TEST(test_opposing_inputs_on_same_axis_cancel_to_zero);
    RUN_TEST(test_diagonal_axes_are_not_pre_normalized);

    return UNITY_END();
}
