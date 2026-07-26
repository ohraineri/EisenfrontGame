#include "eisenfront/input.h"

#include <SDL3/SDL.h>

#include <string.h>

/* KeyCode/MouseButton/GamepadButton/GamepadAxis are defined in input.h to
 * numerically mirror SDL's scancode/button/axis enums, so translation is
 * a zero-cost identity cast instead of a lookup table. These anchors
 * catch it at compile time if that numbering ever drifts. */
static_assert((int)KEY_A == (int)SDL_SCANCODE_A, "KeyCode must mirror SDL_Scancode");
static_assert((int)KEY_Z == (int)SDL_SCANCODE_Z, "KeyCode must mirror SDL_Scancode");
static_assert((int)KEY_0 == (int)SDL_SCANCODE_0, "KeyCode must mirror SDL_Scancode");
static_assert((int)KEY_RETURN == (int)SDL_SCANCODE_RETURN, "KeyCode must mirror SDL_Scancode");
static_assert((int)KEY_F12 == (int)SDL_SCANCODE_F12, "KeyCode must mirror SDL_Scancode");
static_assert((int)KEY_UP == (int)SDL_SCANCODE_UP, "KeyCode must mirror SDL_Scancode");
static_assert((int)KEY_RGUI == (int)SDL_SCANCODE_RGUI, "KeyCode must mirror SDL_Scancode");

static_assert((int)MOUSE_BUTTON_LEFT == SDL_BUTTON_LEFT, "MouseButton must mirror SDL button numbering");
static_assert((int)MOUSE_BUTTON_MIDDLE == SDL_BUTTON_MIDDLE, "MouseButton must mirror SDL button numbering");
static_assert((int)MOUSE_BUTTON_RIGHT == SDL_BUTTON_RIGHT, "MouseButton must mirror SDL button numbering");
static_assert((int)MOUSE_BUTTON_X2 == SDL_BUTTON_X2, "MouseButton must mirror SDL button numbering");

static_assert((int)GAMEPAD_BUTTON_SOUTH == SDL_GAMEPAD_BUTTON_SOUTH, "GamepadButton must mirror SDL_GamepadButton");
static_assert((int)GAMEPAD_BUTTON_DPAD_RIGHT == SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "GamepadButton must mirror SDL_GamepadButton");
static_assert((int)GAMEPAD_AXIS_LEFT_X == SDL_GAMEPAD_AXIS_LEFTX, "GamepadAxis must mirror SDL_GamepadAxis");
static_assert((int)GAMEPAD_AXIS_RIGHT_TRIGGER == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, "GamepadAxis must mirror SDL_GamepadAxis");

#define KEY_SLOT_COUNT 512
#define MAX_GAMEPADS 8

typedef struct GamepadSlot {
    SDL_Gamepad *handle;
    GamepadId    id;
    bool         current_buttons[GAMEPAD_BUTTON_COUNT];
    bool         previous_buttons[GAMEPAD_BUTTON_COUNT];
    float        current_axes[GAMEPAD_AXIS_COUNT];
} GamepadSlot;

static bool g_initialized = false;

static bool g_key_current[KEY_SLOT_COUNT];
static bool g_key_previous[KEY_SLOT_COUNT];

static bool g_mouse_current[MOUSE_BUTTON_COUNT];
static bool g_mouse_previous[MOUSE_BUTTON_COUNT];
static float g_mouse_x, g_mouse_y;
static float g_mouse_dx_accum, g_mouse_dy_accum;
static float g_scroll_dx_accum, g_scroll_dy_accum;

static GamepadSlot g_gamepads[MAX_GAMEPADS];

static GamepadSlot *find_gamepad_slot_by_id(GamepadId id) {
    if (id == INPUT_INVALID_GAMEPAD_ID) {
        return nullptr;
    }
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        if (g_gamepads[i].handle != nullptr && g_gamepads[i].id == id) {
            return &g_gamepads[i];
        }
    }
    return nullptr;
}

static void gamepad_open(SDL_JoystickID which) {
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        if (g_gamepads[i].handle == nullptr) {
            SDL_Gamepad *handle = SDL_OpenGamepad(which);
            if (handle != nullptr) {
                g_gamepads[i] = (GamepadSlot){.handle = handle, .id = which};
            }
            return;
        }
    }
    /* No free slot: silently ignore gamepads beyond MAX_GAMEPADS. */
}

static void gamepad_close(SDL_JoystickID which) {
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        if (g_gamepads[i].handle != nullptr && g_gamepads[i].id == which) {
            SDL_CloseGamepad(g_gamepads[i].handle);
            g_gamepads[i] = (GamepadSlot){0};
            return;
        }
    }
}

static float normalize_axis_value(Sint32 axis, Sint16 value) {
    if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
        return (float)value / 32767.0f;
    }
    return value < 0 ? (float)value / 32768.0f : (float)value / 32767.0f;
}

static void handle_raw_event(void *native_event, void *userdata) {
    (void)userdata;
    const SDL_Event *event = native_event;

    switch (event->type) {
        case SDL_EVENT_KEY_DOWN:
            if (!event->key.repeat && (unsigned)event->key.scancode < KEY_SLOT_COUNT) {
                g_key_current[event->key.scancode] = true;
            }
            break;
        case SDL_EVENT_KEY_UP:
            if ((unsigned)event->key.scancode < KEY_SLOT_COUNT) {
                g_key_current[event->key.scancode] = false;
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event->button.button < MOUSE_BUTTON_COUNT) {
                g_mouse_current[event->button.button] = true;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event->button.button < MOUSE_BUTTON_COUNT) {
                g_mouse_current[event->button.button] = false;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            g_mouse_x = event->motion.x;
            g_mouse_y = event->motion.y;
            g_mouse_dx_accum += event->motion.xrel;
            g_mouse_dy_accum += event->motion.yrel;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            g_scroll_dx_accum += event->wheel.x;
            g_scroll_dy_accum += event->wheel.y;
            break;

        case SDL_EVENT_GAMEPAD_ADDED:
            gamepad_open(event->gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            gamepad_close(event->gdevice.which);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            GamepadSlot *slot = find_gamepad_slot_by_id(event->gbutton.which);
            if (slot != nullptr && event->gbutton.button < GAMEPAD_BUTTON_COUNT) {
                slot->current_buttons[event->gbutton.button] =
                    (event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
            }
            break;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            GamepadSlot *slot = find_gamepad_slot_by_id(event->gaxis.which);
            if (slot != nullptr && event->gaxis.axis < GAMEPAD_AXIS_COUNT) {
                slot->current_axes[event->gaxis.axis] =
                    normalize_axis_value(event->gaxis.axis, event->gaxis.value);
            }
            break;
        }

        default:
            break;
    }
}

Result input_init(void) {
    if (g_initialized) {
        return RESULT_ERROR_ALREADY_INITIALIZED;
    }
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        return RESULT_ERROR_PLATFORM;
    }

    memset(g_key_current, 0, sizeof(g_key_current));
    memset(g_key_previous, 0, sizeof(g_key_previous));
    memset(g_mouse_current, 0, sizeof(g_mouse_current));
    memset(g_mouse_previous, 0, sizeof(g_mouse_previous));
    g_mouse_x = 0.0f;
    g_mouse_y = 0.0f;
    g_mouse_dx_accum = 0.0f;
    g_mouse_dy_accum = 0.0f;
    g_scroll_dx_accum = 0.0f;
    g_scroll_dy_accum = 0.0f;
    memset(g_gamepads, 0, sizeof(g_gamepads));

    window_set_raw_event_callback(handle_raw_event, nullptr);
    g_initialized = true;
    return RESULT_OK;
}

void input_shutdown(void) {
    if (!g_initialized) {
        return;
    }
    window_set_raw_event_callback(nullptr, nullptr);
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        if (g_gamepads[i].handle != nullptr) {
            SDL_CloseGamepad(g_gamepads[i].handle);
        }
    }
    memset(g_gamepads, 0, sizeof(g_gamepads));
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    g_initialized = false;
}

void input_new_frame(void) {
    memcpy(g_key_previous, g_key_current, sizeof(g_key_previous));
    memcpy(g_mouse_previous, g_mouse_current, sizeof(g_mouse_previous));
    g_mouse_dx_accum = 0.0f;
    g_mouse_dy_accum = 0.0f;
    g_scroll_dx_accum = 0.0f;
    g_scroll_dy_accum = 0.0f;
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        if (g_gamepads[i].handle != nullptr) {
            memcpy(g_gamepads[i].previous_buttons, g_gamepads[i].current_buttons,
                   sizeof(g_gamepads[i].current_buttons));
        }
    }
}

/* ==================================================================== *
 * Keyboard
 * ==================================================================== */

bool input_key_pressed(KeyCode key) {
    if ((unsigned)key >= KEY_SLOT_COUNT) {
        return false;
    }
    return g_key_current[key] && !g_key_previous[key];
}

bool input_key_released(KeyCode key) {
    if ((unsigned)key >= KEY_SLOT_COUNT) {
        return false;
    }
    return !g_key_current[key] && g_key_previous[key];
}

bool input_key_held(KeyCode key) {
    if ((unsigned)key >= KEY_SLOT_COUNT) {
        return false;
    }
    return g_key_current[key];
}

/* ==================================================================== *
 * Mouse
 * ==================================================================== */

bool input_mouse_button_pressed(MouseButton button) {
    if (button == 0 || button >= MOUSE_BUTTON_COUNT) {
        return false;
    }
    return g_mouse_current[button] && !g_mouse_previous[button];
}

bool input_mouse_button_released(MouseButton button) {
    if (button == 0 || button >= MOUSE_BUTTON_COUNT) {
        return false;
    }
    return !g_mouse_current[button] && g_mouse_previous[button];
}

bool input_mouse_button_held(MouseButton button) {
    if (button == 0 || button >= MOUSE_BUTTON_COUNT) {
        return false;
    }
    return g_mouse_current[button];
}

void input_mouse_position(float *out_x, float *out_y) {
    if (out_x != nullptr) {
        *out_x = g_mouse_x;
    }
    if (out_y != nullptr) {
        *out_y = g_mouse_y;
    }
}

void input_mouse_delta(float *out_dx, float *out_dy) {
    if (out_dx != nullptr) {
        *out_dx = g_mouse_dx_accum;
    }
    if (out_dy != nullptr) {
        *out_dy = g_mouse_dy_accum;
    }
}

void input_mouse_scroll_delta(float *out_x, float *out_y) {
    if (out_x != nullptr) {
        *out_x = g_scroll_dx_accum;
    }
    if (out_y != nullptr) {
        *out_y = g_scroll_dy_accum;
    }
}

Result input_set_mouse_relative_mode(Window *window, bool enabled) {
    if (window == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    SDL_Window *sdl_window = window_get_native_handle(window);
    if (!SDL_SetWindowRelativeMouseMode(sdl_window, enabled)) {
        return RESULT_ERROR_PLATFORM;
    }
    return RESULT_OK;
}

/* ==================================================================== *
 * Gamepad
 * ==================================================================== */

int32_t input_gamepad_count(void) {
    int32_t count = 0;
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        if (g_gamepads[i].handle != nullptr) {
            count += 1;
        }
    }
    return count;
}

GamepadId input_gamepad_id_at(int32_t slot_index) {
    int32_t seen = 0;
    for (int i = 0; i < MAX_GAMEPADS; ++i) {
        if (g_gamepads[i].handle != nullptr) {
            if (seen == slot_index) {
                return g_gamepads[i].id;
            }
            seen += 1;
        }
    }
    return INPUT_INVALID_GAMEPAD_ID;
}

bool input_gamepad_is_connected(GamepadId id) {
    return find_gamepad_slot_by_id(id) != nullptr;
}

const char *input_gamepad_name(GamepadId id) {
    GamepadSlot *slot = find_gamepad_slot_by_id(id);
    if (slot == nullptr) {
        return nullptr;
    }
    const char *name = SDL_GetGamepadName(slot->handle);
    return name != nullptr ? name : "Unknown Gamepad";
}

bool input_gamepad_button_pressed(GamepadId id, GamepadButton button) {
    GamepadSlot *slot = find_gamepad_slot_by_id(id);
    if (slot == nullptr || (unsigned)button >= GAMEPAD_BUTTON_COUNT) {
        return false;
    }
    return slot->current_buttons[button] && !slot->previous_buttons[button];
}

bool input_gamepad_button_released(GamepadId id, GamepadButton button) {
    GamepadSlot *slot = find_gamepad_slot_by_id(id);
    if (slot == nullptr || (unsigned)button >= GAMEPAD_BUTTON_COUNT) {
        return false;
    }
    return !slot->current_buttons[button] && slot->previous_buttons[button];
}

bool input_gamepad_button_held(GamepadId id, GamepadButton button) {
    GamepadSlot *slot = find_gamepad_slot_by_id(id);
    if (slot == nullptr || (unsigned)button >= GAMEPAD_BUTTON_COUNT) {
        return false;
    }
    return slot->current_buttons[button];
}

float input_gamepad_axis(GamepadId id, GamepadAxis axis) {
    GamepadSlot *slot = find_gamepad_slot_by_id(id);
    if (slot == nullptr || (unsigned)axis >= GAMEPAD_AXIS_COUNT) {
        return 0.0f;
    }
    return slot->current_axes[axis];
}

Result input_gamepad_set_vibration(GamepadId id, float low_frequency, float high_frequency,
                                    uint32_t duration_ms) {
    GamepadSlot *slot = find_gamepad_slot_by_id(id);
    if (slot == nullptr) {
        return RESULT_ERROR_NOT_FOUND;
    }
    const Uint16 low = (Uint16)(SDL_clamp(low_frequency, 0.0f, 1.0f) * 65535.0f);
    const Uint16 high = (Uint16)(SDL_clamp(high_frequency, 0.0f, 1.0f) * 65535.0f);
    if (!SDL_RumbleGamepad(slot->handle, low, high, duration_ms)) {
        return RESULT_ERROR_NOT_SUPPORTED;
    }
    return RESULT_OK;
}
