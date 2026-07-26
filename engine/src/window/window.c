#include "eisenfront/window.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>

#define WINDOW_LOG_CATEGORY "window"
#define WINDOW_DATA_KEY "ef_window"

struct Window {
    SDL_Window   *sdl_window;
    WindowMode    mode;
    bool          should_close;
    WindowEventFn event_fn;
    void         *event_userdata;
};

static bool             g_initialized = false;
static int32_t          g_window_count = 0;
static WindowRawEventFn g_raw_event_fn = nullptr;
static void             *g_raw_event_userdata = nullptr;

Result window_init(void) {
    if (g_initialized) {
        return RESULT_ERROR_ALREADY_INITIALIZED;
    }
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        return RESULT_ERROR_PLATFORM;
    }
    g_initialized = true;
    return RESULT_OK;
}

void window_shutdown(void) {
    if (!g_initialized) {
        return;
    }
    ASSERT_MSG(g_window_count == 0, "window_shutdown called with live windows still open");
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    g_initialized = false;
}

/* ==================================================================== *
 * Monitors
 * ==================================================================== */

static Result get_display_id_by_index(int32_t index, SDL_DisplayID *out_id) {
    int             count = 0;
    SDL_DisplayID  *ids = SDL_GetDisplays(&count);
    if (ids == nullptr) {
        return RESULT_ERROR_PLATFORM;
    }

    Result result = RESULT_ERROR_NOT_FOUND;
    if (index >= 0 && index < count) {
        *out_id = ids[index];
        result = RESULT_OK;
    }
    SDL_free(ids);
    return result;
}

int32_t monitor_get_count(void) {
    int            count = 0;
    SDL_DisplayID *ids = SDL_GetDisplays(&count);
    SDL_free(ids);
    return (int32_t)count;
}

Result monitor_get_info(int32_t index, MonitorInfo *out_info) {
    if (out_info == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    SDL_DisplayID display_id;
    Result        result = get_display_id_by_index(index, &display_id);
    if (result != RESULT_OK) {
        return result;
    }

    SDL_Rect bounds;
    if (!SDL_GetDisplayBounds(display_id, &bounds)) {
        return RESULT_ERROR_PLATFORM;
    }

    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(display_id);
    if (mode == nullptr) {
        return RESULT_ERROR_PLATFORM;
    }

    const char *name = SDL_GetDisplayName(display_id);

    out_info->index = index;
    snprintf(out_info->name, sizeof(out_info->name), "%s", name != nullptr ? name : "Unknown");
    out_info->x = bounds.x;
    out_info->y = bounds.y;
    out_info->width = bounds.w;
    out_info->height = bounds.h;
    out_info->refresh_rate_hz = mode->refresh_rate;
    out_info->display_scale = SDL_GetDisplayContentScale(display_id);
    out_info->is_primary = (display_id == SDL_GetPrimaryDisplay());

    return RESULT_OK;
}

/* ==================================================================== *
 * Window
 * ==================================================================== */

Result window_create(const WindowDesc *desc, Window **out_window) {
    if (desc == nullptr || out_window == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return RESULT_ERROR_NOT_INITIALIZED;
    }
    if (desc->title == nullptr || desc->width <= 0 || desc->height <= 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    /* SDL_WINDOW_OPENGL must be set at creation time: the future Graphics
     * Context module attaches a GL context to this window and SDL cannot
     * add that capability after the fact. SDL_WINDOW_HIGH_PIXEL_DENSITY
     * requests native-resolution pixels on HiDPI displays. */
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc->resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (desc->mode == WINDOW_MODE_BORDERLESS) {
        flags |= SDL_WINDOW_BORDERLESS;
    }

    SDL_Window *sdl_window = SDL_CreateWindow(desc->title, desc->width, desc->height, flags);
    if (sdl_window == nullptr) {
        LOG_ERROR(WINDOW_LOG_CATEGORY, "SDL_CreateWindow failed: %s", SDL_GetError());
        return RESULT_ERROR_PLATFORM;
    }

    Window *window = malloc(sizeof(*window));
    if (window == nullptr) {
        SDL_DestroyWindow(sdl_window);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    window->sdl_window = sdl_window;
    window->mode = WINDOW_MODE_WINDOWED;
    window->should_close = false;
    window->event_fn = nullptr;
    window->event_userdata = nullptr;

    SDL_SetPointerProperty(SDL_GetWindowProperties(sdl_window), WINDOW_DATA_KEY, window);
    g_window_count += 1;

    Result mode_result = RESULT_OK;
    if (desc->mode != WINDOW_MODE_WINDOWED) {
        mode_result = window_set_mode(window, desc->mode, desc->monitor_index);
    }
    if (mode_result != RESULT_OK) {
        window_destroy(window);
        return mode_result;
    }

    *out_window = window;
    return RESULT_OK;
}

void window_destroy(Window *window) {
    if (window == nullptr) {
        return;
    }
    SDL_DestroyWindow(window->sdl_window);
    free(window);
    g_window_count -= 1;
}

Result window_set_title(Window *window, const char *title) {
    if (window == nullptr || title == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!SDL_SetWindowTitle(window->sdl_window, title)) {
        return RESULT_ERROR_PLATFORM;
    }
    return RESULT_OK;
}

Result window_set_mode(Window *window, WindowMode mode, int32_t monitor_index) {
    if (window == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    if (monitor_index >= 0) {
        SDL_DisplayID display_id;
        Result        result = get_display_id_by_index(monitor_index, &display_id);
        if (result != RESULT_OK) {
            return result;
        }
        SDL_Rect bounds;
        if (SDL_GetDisplayBounds(display_id, &bounds)) {
            /* Move the window onto the target monitor before switching
             * modes, so SDL's "nearest display" fullscreen logic resolves
             * to the monitor the caller actually asked for. */
            SDL_SetWindowPosition(window->sdl_window, bounds.x + 32, bounds.y + 32);
        }
    }

    switch (mode) {
        case WINDOW_MODE_WINDOWED:
            SDL_SetWindowFullscreen(window->sdl_window, false);
            SDL_SetWindowBordered(window->sdl_window, true);
            break;

        case WINDOW_MODE_BORDERLESS:
            SDL_SetWindowFullscreen(window->sdl_window, false);
            SDL_SetWindowBordered(window->sdl_window, false);
            break;

        case WINDOW_MODE_FULLSCREEN_BORDERLESS:
            /* A nullptr mode tells SDL3 to use borderless-desktop
             * fullscreen (fills the display without a video mode
             * change). */
            SDL_SetWindowFullscreenMode(window->sdl_window, nullptr);
            if (!SDL_SetWindowFullscreen(window->sdl_window, true)) {
                return RESULT_ERROR_PLATFORM;
            }
            break;

        case WINDOW_MODE_FULLSCREEN: {
            const SDL_DisplayID display_id = SDL_GetDisplayForWindow(window->sdl_window);
            const SDL_DisplayMode *desktop_mode = SDL_GetDesktopDisplayMode(display_id);
            if (desktop_mode == nullptr) {
                return RESULT_ERROR_PLATFORM;
            }
            if (!SDL_SetWindowFullscreenMode(window->sdl_window, desktop_mode)) {
                return RESULT_ERROR_PLATFORM;
            }
            if (!SDL_SetWindowFullscreen(window->sdl_window, true)) {
                return RESULT_ERROR_PLATFORM;
            }
            break;
        }

        default:
            return RESULT_ERROR_INVALID_ARGUMENT;
    }

    window->mode = mode;
    return RESULT_OK;
}

WindowMode window_get_mode(const Window *window) {
    ASSERT(window != nullptr);
    return window != nullptr ? window->mode : WINDOW_MODE_WINDOWED;
}

Result window_set_size(Window *window, int32_t width, int32_t height) {
    if (window == nullptr || width <= 0 || height <= 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (window->mode == WINDOW_MODE_FULLSCREEN || window->mode == WINDOW_MODE_FULLSCREEN_BORDERLESS) {
        return RESULT_ERROR_INVALID_STATE;
    }
    if (!SDL_SetWindowSize(window->sdl_window, width, height)) {
        return RESULT_ERROR_PLATFORM;
    }
    return RESULT_OK;
}

void window_get_size(const Window *window, int32_t *out_width, int32_t *out_height) {
    ASSERT(window != nullptr);
    if (window == nullptr) {
        return;
    }
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window->sdl_window, &w, &h);
    if (out_width != nullptr) {
        *out_width = w;
    }
    if (out_height != nullptr) {
        *out_height = h;
    }
}

void window_get_size_in_pixels(const Window *window, int32_t *out_width, int32_t *out_height) {
    ASSERT(window != nullptr);
    if (window == nullptr) {
        return;
    }
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window->sdl_window, &w, &h);
    if (out_width != nullptr) {
        *out_width = w;
    }
    if (out_height != nullptr) {
        *out_height = h;
    }
}

float window_get_display_scale(const Window *window) {
    ASSERT(window != nullptr);
    if (window == nullptr) {
        return 1.0f;
    }
    return SDL_GetWindowDisplayScale(window->sdl_window);
}

void window_set_resizable(Window *window, bool resizable) {
    ASSERT(window != nullptr);
    if (window == nullptr) {
        return;
    }
    SDL_SetWindowResizable(window->sdl_window, resizable);
}

bool window_should_close(const Window *window) {
    ASSERT(window != nullptr);
    return window != nullptr ? window->should_close : true;
}

void window_request_close(Window *window) {
    ASSERT(window != nullptr);
    if (window != nullptr) {
        window->should_close = true;
    }
}

void *window_get_native_handle(const Window *window) {
    ASSERT(window != nullptr);
    return window != nullptr ? (void *)window->sdl_window : nullptr;
}

/* ==================================================================== *
 * Events
 * ==================================================================== */

void window_set_event_callback(Window *window, WindowEventFn fn, void *userdata) {
    ASSERT(window != nullptr);
    if (window == nullptr) {
        return;
    }
    window->event_fn = fn;
    window->event_userdata = userdata;
}

void window_set_raw_event_callback(WindowRawEventFn fn, void *userdata) {
    g_raw_event_fn = fn;
    g_raw_event_userdata = userdata;
}

static Window *window_from_id(SDL_WindowID id) {
    SDL_Window *sdl_window = SDL_GetWindowFromID(id);
    if (sdl_window == nullptr) {
        return nullptr;
    }
    return SDL_GetPointerProperty(SDL_GetWindowProperties(sdl_window), WINDOW_DATA_KEY, nullptr);
}

static void dispatch_window_event(Window *window, const WindowEventData *event) {
    if (window != nullptr && window->event_fn != nullptr) {
        window->event_fn(event, window->event_userdata);
    }
}

void window_poll_events(void) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (g_raw_event_fn != nullptr) {
            g_raw_event_fn(&event, g_raw_event_userdata);
        }

        Window *window = nullptr;
        WindowEventData data = {0};

        switch (event.type) {
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                window = window_from_id(event.window.windowID);
                if (window != nullptr) {
                    window->should_close = true;
                }
                data = (WindowEventData){.type = WINDOW_EVENT_CLOSE_REQUESTED, .window = window};
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                window = window_from_id(event.window.windowID);
                data = (WindowEventData){.type = WINDOW_EVENT_RESIZED,
                                          .window = window,
                                          .width = event.window.data1,
                                          .height = event.window.data2};
                break;
            case SDL_EVENT_WINDOW_MOVED:
                window = window_from_id(event.window.windowID);
                data = (WindowEventData){.type = WINDOW_EVENT_MOVED,
                                          .window = window,
                                          .x = event.window.data1,
                                          .y = event.window.data2};
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                window = window_from_id(event.window.windowID);
                data = (WindowEventData){.type = WINDOW_EVENT_FOCUS_GAINED, .window = window};
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                window = window_from_id(event.window.windowID);
                data = (WindowEventData){.type = WINDOW_EVENT_FOCUS_LOST, .window = window};
                break;
            case SDL_EVENT_WINDOW_MINIMIZED:
                window = window_from_id(event.window.windowID);
                data = (WindowEventData){.type = WINDOW_EVENT_MINIMIZED, .window = window};
                break;
            case SDL_EVENT_WINDOW_MAXIMIZED:
                window = window_from_id(event.window.windowID);
                data = (WindowEventData){.type = WINDOW_EVENT_MAXIMIZED, .window = window};
                break;
            case SDL_EVENT_WINDOW_RESTORED:
                window = window_from_id(event.window.windowID);
                data = (WindowEventData){.type = WINDOW_EVENT_RESTORED, .window = window};
                break;
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                window = window_from_id(event.window.windowID);
                data = (WindowEventData){.type = WINDOW_EVENT_DISPLAY_CHANGED, .window = window};
                break;
            default:
                continue;
        }

        dispatch_window_event(window, &data);
    }
}
