/*
 * Runs against SDL3's "offscreen" video driver (see CMakeLists.txt) so it
 * passes in headless CI. Events are synthesized with SDL_PushEvent and
 * consumed through the real window_poll_events() -> Input raw-callback
 * path, exercising the same code a real keyboard/mouse would drive.
 */
#include "eisenfront/input.h"

#include "unity.h"

#include <SDL3/SDL.h>

void setUp(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());
    TEST_ASSERT_EQUAL(RESULT_OK, input_init());
}

void tearDown(void) {
    input_shutdown();
    window_shutdown();
}

static void push_key_event(bool down, SDL_Scancode scancode) {
    SDL_Event event = {0};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.type = (Uint32)event.type;
    event.key.scancode = scancode;
    event.key.down = down;
    event.key.repeat = false;
    SDL_PushEvent(&event);
}

static void push_mouse_button_event(bool down, Uint8 button) {
    SDL_Event event = {0};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.type = (Uint32)event.type;
    event.button.button = button;
    event.button.down = down;
    SDL_PushEvent(&event);
}

static void push_mouse_motion_event(float x, float y, float xrel, float yrel) {
    SDL_Event event = {0};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = (Uint32)event.type;
    event.motion.x = x;
    event.motion.y = y;
    event.motion.xrel = xrel;
    event.motion.yrel = yrel;
    SDL_PushEvent(&event);
}

static void push_mouse_wheel_event(float x, float y) {
    SDL_Event event = {0};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.type = (Uint32)event.type;
    event.wheel.x = x;
    event.wheel.y = y;
    SDL_PushEvent(&event);
}

/* ---- Keyboard -------------------------------------------------------- */

static void test_key_press_hold_release_cycle(void) {
    push_key_event(true, SDL_SCANCODE_A);
    input_new_frame();
    window_poll_events();
    TEST_ASSERT_TRUE(input_key_pressed(KEY_A));
    TEST_ASSERT_TRUE(input_key_held(KEY_A));
    TEST_ASSERT_FALSE(input_key_released(KEY_A));

    /* Next frame, still held, no new event: pressed edge must not repeat. */
    input_new_frame();
    window_poll_events();
    TEST_ASSERT_FALSE(input_key_pressed(KEY_A));
    TEST_ASSERT_TRUE(input_key_held(KEY_A));

    push_key_event(false, SDL_SCANCODE_A);
    input_new_frame();
    window_poll_events();
    TEST_ASSERT_TRUE(input_key_released(KEY_A));
    TEST_ASSERT_FALSE(input_key_held(KEY_A));

    input_new_frame();
    window_poll_events();
    TEST_ASSERT_FALSE(input_key_released(KEY_A));
    TEST_ASSERT_FALSE(input_key_held(KEY_A));
}

static void test_untouched_key_is_never_down(void) {
    input_new_frame();
    window_poll_events();
    TEST_ASSERT_FALSE(input_key_held(KEY_Z));
    TEST_ASSERT_FALSE(input_key_pressed(KEY_Z));
    TEST_ASSERT_FALSE(input_key_released(KEY_Z));
}

/* ---- Mouse -------------------------------------------------------------- */

static void test_mouse_button_cycle(void) {
    push_mouse_button_event(true, MOUSE_BUTTON_LEFT);
    input_new_frame();
    window_poll_events();
    TEST_ASSERT_TRUE(input_mouse_button_pressed(MOUSE_BUTTON_LEFT));
    TEST_ASSERT_TRUE(input_mouse_button_held(MOUSE_BUTTON_LEFT));

    push_mouse_button_event(false, MOUSE_BUTTON_LEFT);
    input_new_frame();
    window_poll_events();
    TEST_ASSERT_TRUE(input_mouse_button_released(MOUSE_BUTTON_LEFT));
    TEST_ASSERT_FALSE(input_mouse_button_held(MOUSE_BUTTON_LEFT));
}

static void test_mouse_motion_updates_position_and_delta(void) {
    push_mouse_motion_event(100.0f, 200.0f, 5.0f, -3.0f);
    input_new_frame();
    window_poll_events();

    float x = 0.0f, y = 0.0f, dx = 0.0f, dy = 0.0f;
    input_mouse_position(&x, &y);
    input_mouse_delta(&dx, &dy);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, x);
    TEST_ASSERT_EQUAL_FLOAT(200.0f, y);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, dx);
    TEST_ASSERT_EQUAL_FLOAT(-3.0f, dy);

    /* Delta must reset each frame even though position is sticky. */
    input_new_frame();
    window_poll_events();
    input_mouse_position(&x, &y);
    input_mouse_delta(&dx, &dy);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, x);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dx);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, dy);
}

static void test_mouse_scroll_accumulates_and_resets(void) {
    push_mouse_wheel_event(1.0f, 2.0f);
    push_mouse_wheel_event(0.5f, -1.0f);
    input_new_frame();
    window_poll_events();

    float x = 0.0f, y = 0.0f;
    input_mouse_scroll_delta(&x, &y);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, x);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, y);

    input_new_frame();
    window_poll_events();
    input_mouse_scroll_delta(&x, &y);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, x);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, y);
}

/* ---- Gamepad (no hardware in CI: only the "nothing connected" path) ----- */

static void test_gamepad_queries_are_safe_with_none_connected(void) {
    TEST_ASSERT_EQUAL(0, input_gamepad_count());
    TEST_ASSERT_EQUAL(INPUT_INVALID_GAMEPAD_ID, input_gamepad_id_at(0));
    TEST_ASSERT_FALSE(input_gamepad_is_connected(INPUT_INVALID_GAMEPAD_ID));
    TEST_ASSERT_FALSE(input_gamepad_button_held(1, GAMEPAD_BUTTON_SOUTH));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, input_gamepad_axis(1, GAMEPAD_AXIS_LEFT_X));
    TEST_ASSERT_EQUAL(RESULT_ERROR_NOT_FOUND, input_gamepad_set_vibration(1, 1.0f, 1.0f, 100));
}

/* ---- input_process_raw_event() called directly, bypassing Window's
 * own callback slot - the exact usage a caller chaining Input with
 * another raw-event consumer (e.g. Editor) needs. ---- */

static void test_process_raw_event_directly_updates_state(void) {
    SDL_Event event = {0};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.type = (Uint32)event.type;
    event.key.scancode = SDL_SCANCODE_A;
    event.key.down = true;
    event.key.repeat = false;

    input_new_frame();
    input_process_raw_event(&event, nullptr);
    TEST_ASSERT_TRUE(input_key_pressed(KEY_A));
    TEST_ASSERT_TRUE(input_key_held(KEY_A));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_key_press_hold_release_cycle);
    RUN_TEST(test_untouched_key_is_never_down);
    RUN_TEST(test_mouse_button_cycle);
    RUN_TEST(test_mouse_motion_updates_position_and_delta);
    RUN_TEST(test_mouse_scroll_accumulates_and_resets);
    RUN_TEST(test_gamepad_queries_are_safe_with_none_connected);
    RUN_TEST(test_process_raw_event_directly_updates_state);

    return UNITY_END();
}
