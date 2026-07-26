/*
 * Outpost - the Eisenfront engine's vertical slice (see
 * Docs/ and the plan this was built from for what it's proving).
 *
 * Phase 1: module lifecycle scaffolding only. Opens a real window,
 * pumps input, clears the screen every frame, quits on ESC or the
 * window's close button. Every later phase builds inside this loop -
 * nothing here is torn down and rebuilt differently later.
 */
#include "eisenfront/core.h"
#include "eisenfront/graphics_context.h"
#include "eisenfront/input.h"
#include "eisenfront/renderer.h"
#include "eisenfront/window.h"

#include "eisenfront/audio.h"

#include <stdlib.h>

/* Editor is not wired in yet - see Phase 8. Both input_init() and
 * editor_init() claim Window's single raw-event callback slot (there
 * is no built-in chaining - see window.h/editor.h), so bringing Editor
 * in here would silently break WASD/mouse-look the moment
 * editor_init() overwrites Input's registration. Phase 8 fixes this at
 * the engine level (a public "process one raw event" entry point on
 * both Input and Editor) rather than papering over it here. */

#define OUTPOST_LOG_CATEGORY "outpost"

int main(void) {
    log_init(LOG_LEVEL_INFO);
    LOG_INFO(OUTPOST_LOG_CATEGORY, "Outpost starting");

    if (window_init() != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "window_init failed");
        return 1;
    }
    if (input_init() != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "input_init failed");
        window_shutdown();
        return 1;
    }

    const WindowDesc window_desc = {
        .title = "Eisenfront - Outpost",
        .width = 1280,
        .height = 720,
        .mode = WINDOW_MODE_WINDOWED,
        .resizable = true,
        .monitor_index = -1,
    };
    Window *window = nullptr;
    if (window_create(&window_desc, &window) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "window_create failed");
        input_shutdown();
        window_shutdown();
        return 1;
    }

    GraphicsContextDesc context_desc = graphics_context_desc_default();
    context_desc.debug = true;
    context_desc.vsync = VSYNC_ON;
    GraphicsContext *context = nullptr;
    if (graphics_context_create(window, &context_desc, &context) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "graphics_context_create failed");
        window_destroy(window);
        input_shutdown();
        window_shutdown();
        return 1;
    }

    Renderer *renderer = nullptr;
    if (renderer_create(context, nullptr, &renderer) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "renderer_create failed");
        graphics_context_destroy(context);
        window_destroy(window);
        input_shutdown();
        window_shutdown();
        return 1;
    }

    AudioEngineDesc audio_desc = audio_engine_desc_default();
    AudioEngine    *audio = nullptr;
    if (audio_engine_create(&audio_desc, &audio) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "audio_engine_create failed");
        renderer_destroy(renderer);
        graphics_context_destroy(context);
        window_destroy(window);
        input_shutdown();
        window_shutdown();
        return 1;
    }

    FrameClock clock;
    frame_clock_init(&clock);

    /* Headless verification hook: this sandbox has no real display or
     * input device to close the window by hand. OUTPOST_MAX_FRAMES, if
     * set, bounds the loop so the frame lifecycle (every module's
     * per-frame call, every destroy on the way out) can be proven to
     * run cleanly without a human present. Unset in a normal play
     * session - the window's own close button and ESC still work. This
     * becomes the seed of the --smoke-test mode in the final pass. */
    const char *max_frames_env = getenv("OUTPOST_MAX_FRAMES");
    const long  max_frames = (max_frames_env != nullptr) ? strtol(max_frames_env, nullptr, 10) : 0;
    long        frame_count = 0;

    LOG_INFO(OUTPOST_LOG_CATEGORY, "Entering frame loop");
    while (!window_should_close(window)) {
        input_new_frame();
        window_poll_events();

        if (input_key_pressed(KEY_ESCAPE)) {
            window_request_close(window);
        }

        frame_clock_tick(&clock);

        int32_t width, height;
        window_get_size_in_pixels(window, &width, &height);

        renderer_begin_frame(renderer);
        renderer_set_viewport(renderer, 0, 0, width, height);
        const ClearParams clear = {
            .flags = CLEAR_FLAG_COLOR | CLEAR_FLAG_DEPTH,
            .red = 0.05f,
            .green = 0.07f,
            .blue = 0.09f,
            .alpha = 1.0f,
            .depth = 1.0f,
            .stencil = 0,
        };
        renderer_clear(renderer, &clear);
        renderer_end_frame(renderer);

        audio_engine_update(audio);
        graphics_context_swap_buffers(context);

        frame_count += 1;
        if (max_frames > 0 && frame_count >= max_frames) {
            window_request_close(window);
        }
    }

    LOG_INFO(OUTPOST_LOG_CATEGORY, "Shutting down");

    audio_engine_destroy(audio);
    renderer_destroy(renderer);
    graphics_context_destroy(context);
    window_destroy(window);
    input_shutdown();
    window_shutdown();
    log_shutdown();
    return 0;
}
