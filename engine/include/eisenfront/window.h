/*
 * Window - window creation and management, backed by SDL3.
 *
 * Responsibility: own the OS window and its display-server concerns
 * (fullscreen, borderless, monitor selection, resize, DPI, window
 * events) behind a neutral API. No SDL type or header is exposed here;
 * "the renderer must not depend directly on SDL" holds transitively
 * through Window as well as through Graphics Context.
 *
 * The one deliberate exception is window_get_native_handle(): it hands
 * back the underlying SDL_Window* as an opaque void*, solely so the
 * Graphics Context module (which also links SDL3 privately, to create
 * the GL context on this window) can reach it without this header
 * including any SDL type. C has no access control to enforce this, so it
 * is enforced by convention: gameplay and Renderer code must never call
 * it. The same reasoning applies to window_set_raw_event_callback(),
 * which is the seam the future Input module uses to observe every SDL
 * event (keyboard/mouse/gamepad) without Window needing to know Input
 * exists.
 *
 * Dependencies: Core (Result) and SDL3 (privately, in window.c only).
 *
 * Lifetime: window_init() must be called once before any window_create()
 * call and window_shutdown() once after every Window has been destroyed.
 * Multiple windows may be live at once; SDL3 is natively multi-window.
 */
#ifndef EISENFRONT_WINDOW_H
#define EISENFRONT_WINDOW_H

#include "core.h"

ENGINE_API Result window_init(void);
ENGINE_API void   window_shutdown(void);

/* ==================================================================== *
 * Monitors
 * ==================================================================== */

typedef struct MonitorInfo {
    int32_t index;
    char    name[128];
    int32_t x, y;                /* position in the virtual desktop, window coordinates */
    int32_t width, height;       /* window coordinates, not necessarily physical pixels */
    float   refresh_rate_hz;
    float   display_scale;       /* 1.0 == 96 DPI baseline */
    bool    is_primary;
} MonitorInfo;

ENGINE_API int32_t monitor_get_count(void);
ENGINE_API Result   monitor_get_info(int32_t index, MonitorInfo *out_info);

/* ==================================================================== *
 * Window
 * ==================================================================== */

typedef enum WindowMode {
    WINDOW_MODE_WINDOWED = 0,
    WINDOW_MODE_BORDERLESS,            /* windowed, no decorations */
    WINDOW_MODE_FULLSCREEN,            /* exclusive: changes the display's video mode */
    WINDOW_MODE_FULLSCREEN_BORDERLESS, /* borderless window filling the display's desktop mode */
} WindowMode;

typedef struct WindowDesc {
    const char *title;
    int32_t     width;
    int32_t     height;
    WindowMode  mode;
    bool        resizable;
    int32_t     monitor_index; /* -1 selects the primary monitor */
} WindowDesc;

typedef struct Window Window;

ENGINE_API Result window_create(const WindowDesc *desc, Window **out_window);
ENGINE_API void   window_destroy(Window *window);

ENGINE_API Result window_set_title(Window *window, const char *title);

ENGINE_API Result     window_set_mode(Window *window, WindowMode mode, int32_t monitor_index);
ENGINE_API WindowMode window_get_mode(const Window *window);

ENGINE_API Result window_set_size(Window *window, int32_t width, int32_t height);
ENGINE_API void   window_get_size(const Window *window, int32_t *out_width, int32_t *out_height);
ENGINE_API void   window_get_size_in_pixels(const Window *window, int32_t *out_width,
                                             int32_t *out_height);
ENGINE_API float  window_get_display_scale(const Window *window);

ENGINE_API void window_set_resizable(Window *window, bool resizable);

ENGINE_API bool window_should_close(const Window *window);
ENGINE_API void window_request_close(Window *window);

/* Opaque SDL_Window* — see the file header comment. */
ENGINE_API void *window_get_native_handle(const Window *window);

/* ==================================================================== *
 * Events
 *
 * window_poll_events() pumps every pending event for every live window
 * once; call it exactly once per frame from the engine loop. Callbacks
 * fire synchronously from within that call, never from another thread.
 * ==================================================================== */

typedef enum WindowEventType {
    WINDOW_EVENT_RESIZED,
    WINDOW_EVENT_CLOSE_REQUESTED,
    WINDOW_EVENT_FOCUS_GAINED,
    WINDOW_EVENT_FOCUS_LOST,
    WINDOW_EVENT_MINIMIZED,
    WINDOW_EVENT_MAXIMIZED,
    WINDOW_EVENT_RESTORED,
    WINDOW_EVENT_MOVED,
    WINDOW_EVENT_DISPLAY_CHANGED,
} WindowEventType;

typedef struct WindowEventData {
    WindowEventType type;
    Window         *window;
    /* Populated for WINDOW_EVENT_RESIZED only. */
    int32_t width;
    int32_t height;
    /* Populated for WINDOW_EVENT_MOVED only. */
    int32_t x;
    int32_t y;
} WindowEventData;

typedef void (*WindowEventFn)(const WindowEventData *event, void *userdata);

ENGINE_API void window_set_event_callback(Window *window, WindowEventFn fn, void *userdata);

/* Internal engine seam (see file header comment): native_event points at
 * a stack-allocated SDL_Event valid only for the duration of the
 * callback. Process-wide, not per-window - fires once per pumped event,
 * for every event, regardless of which window (if any) it targets. */
typedef void (*WindowRawEventFn)(void *native_event, void *userdata);

ENGINE_API void window_set_raw_event_callback(WindowRawEventFn fn, void *userdata);

ENGINE_API void window_poll_events(void);

#endif /* EISENFRONT_WINDOW_H */
