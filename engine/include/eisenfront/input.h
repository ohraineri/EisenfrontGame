/*
 * Input - keyboard, mouse and gamepad state, backed by SDL3 events.
 *
 * Responsibility: turn the raw event stream Window exposes internally
 * (window_set_raw_event_callback) into simple pressed/released/held
 * queries. No SDL type or header is exposed here; gameplay must never
 * call SDL directly, matching Window's rule for the renderer.
 *
 * Dependencies: Core and Window. input_init() installs itself as
 * Window's process-wide raw event callback, so only one Input consumer
 * may exist per process, same as Window's one-callback-slot design.
 *
 * Frame contract: call, in this exact order, once per frame:
 *
 *   input_new_frame();     // snapshots this frame's state as "previous"
 *                           // and clears the per-frame deltas (mouse
 *                           // delta, scroll delta) ready to accumulate
 *   window_poll_events();  // Window pumps SDL events; Input's raw
 *                           // callback updates "current" state and
 *                           // accumulates this frame's deltas as events
 *                           // arrive
 *   // ... query input_key_pressed/held/released etc ...
 *
 * Calling them in any other order, or more than once per frame, makes
 * "pressed"/"released" edges wrong (they are current-vs-previous-frame
 * comparisons) even though "held" queries would still read correctly.
 */
#ifndef EISENFRONT_INPUT_H
#define EISENFRONT_INPUT_H

#include "core.h"
#include "window.h"

ENGINE_API Result input_init(void);
ENGINE_API void   input_shutdown(void);

ENGINE_API void input_new_frame(void);

/* ==================================================================== *
 * Keyboard
 *
 * KeyCode values are not exhaustive - they cover the keys most games
 * need. Extend as needed; see input.c for how new codes must be added
 * (they are checked against SDL's scancode numbering at compile time).
 * ==================================================================== */

typedef enum KeyCode {
    KEY_A = 4, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
    KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
    KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
    KEY_RETURN, KEY_ESCAPE, KEY_BACKSPACE, KEY_TAB, KEY_SPACE,
    KEY_MINUS, KEY_EQUALS, KEY_LEFTBRACKET, KEY_RIGHTBRACKET, KEY_BACKSLASH,
    KEY_SEMICOLON = 51, KEY_APOSTROPHE, KEY_GRAVE = 53, KEY_COMMA, KEY_PERIOD, KEY_SLASH,
    KEY_CAPSLOCK = 57,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_PRINTSCREEN, KEY_SCROLLLOCK, KEY_PAUSE, KEY_INSERT, KEY_HOME, KEY_PAGEUP,
    KEY_DELETE, KEY_END, KEY_PAGEDOWN, KEY_RIGHT, KEY_LEFT, KEY_DOWN, KEY_UP,
    KEY_LCTRL = 224, KEY_LSHIFT, KEY_LALT, KEY_LGUI, KEY_RCTRL, KEY_RSHIFT, KEY_RALT, KEY_RGUI,
} KeyCode;

ENGINE_API bool input_key_pressed(KeyCode key);  /* true only on the frame it went down */
ENGINE_API bool input_key_released(KeyCode key); /* true only on the frame it went up */
ENGINE_API bool input_key_held(KeyCode key);     /* true every frame it is down */

/* ==================================================================== *
 * Mouse
 * ==================================================================== */

typedef enum MouseButton {
    MOUSE_BUTTON_LEFT = 1,
    MOUSE_BUTTON_MIDDLE,
    MOUSE_BUTTON_RIGHT,
    MOUSE_BUTTON_X1,
    MOUSE_BUTTON_X2,
    MOUSE_BUTTON_COUNT,
} MouseButton;

ENGINE_API bool input_mouse_button_pressed(MouseButton button);
ENGINE_API bool input_mouse_button_released(MouseButton button);
ENGINE_API bool input_mouse_button_held(MouseButton button);

/* Window-space position, updated on motion events. */
ENGINE_API void input_mouse_position(float *out_x, float *out_y);
/* Accumulated relative motion since the last input_new_frame(). In
 * relative mouse mode (input_set_mouse_relative_mode) this keeps
 * reporting motion even once the cursor is locked to the window center. */
ENGINE_API void input_mouse_delta(float *out_dx, float *out_dy);
/* Accumulated scroll since the last input_new_frame(). */
ENGINE_API void input_mouse_scroll_delta(float *out_x, float *out_y);

/* Locks and hides the cursor to the window, reporting motion only
 * through input_mouse_delta() - the usual FPS camera-look setup. */
ENGINE_API Result input_set_mouse_relative_mode(Window *window, bool enabled);

/* ==================================================================== *
 * Gamepad
 * ==================================================================== */

typedef uint32_t GamepadId;
#define INPUT_INVALID_GAMEPAD_ID ((GamepadId)0)

typedef enum GamepadButton {
    GAMEPAD_BUTTON_SOUTH = 0,
    GAMEPAD_BUTTON_EAST,
    GAMEPAD_BUTTON_WEST,
    GAMEPAD_BUTTON_NORTH,
    GAMEPAD_BUTTON_BACK,
    GAMEPAD_BUTTON_GUIDE,
    GAMEPAD_BUTTON_START,
    GAMEPAD_BUTTON_LEFT_STICK,
    GAMEPAD_BUTTON_RIGHT_STICK,
    GAMEPAD_BUTTON_LEFT_SHOULDER,
    GAMEPAD_BUTTON_RIGHT_SHOULDER,
    GAMEPAD_BUTTON_DPAD_UP,
    GAMEPAD_BUTTON_DPAD_DOWN,
    GAMEPAD_BUTTON_DPAD_LEFT,
    GAMEPAD_BUTTON_DPAD_RIGHT,
    GAMEPAD_BUTTON_COUNT,
} GamepadButton;

typedef enum GamepadAxis {
    GAMEPAD_AXIS_LEFT_X = 0,
    GAMEPAD_AXIS_LEFT_Y,
    GAMEPAD_AXIS_RIGHT_X,
    GAMEPAD_AXIS_RIGHT_Y,
    GAMEPAD_AXIS_LEFT_TRIGGER,
    GAMEPAD_AXIS_RIGHT_TRIGGER,
    GAMEPAD_AXIS_COUNT,
} GamepadAxis;

ENGINE_API int32_t  input_gamepad_count(void);
ENGINE_API GamepadId input_gamepad_id_at(int32_t slot_index); /* INPUT_INVALID_GAMEPAD_ID if out of range */
ENGINE_API bool      input_gamepad_is_connected(GamepadId id);
ENGINE_API const char *input_gamepad_name(GamepadId id);

ENGINE_API bool  input_gamepad_button_pressed(GamepadId id, GamepadButton button);
ENGINE_API bool  input_gamepad_button_released(GamepadId id, GamepadButton button);
ENGINE_API bool  input_gamepad_button_held(GamepadId id, GamepadButton button);
ENGINE_API float input_gamepad_axis(GamepadId id, GamepadAxis axis); /* -1..1, triggers 0..1 */

/* low_frequency drives strong/rumble motors, high_frequency drives
 * weak/precision motors; both 0..1. duration_ms == 0 stops vibration. */
ENGINE_API Result input_gamepad_set_vibration(GamepadId id, float low_frequency,
                                               float high_frequency, uint32_t duration_ms);

#endif /* EISENFRONT_INPUT_H */
