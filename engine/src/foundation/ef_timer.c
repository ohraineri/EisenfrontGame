#include "eisenfront/foundation/ef_timer.h"

#include <SDL3/SDL_timer.h>

static bool g_initialized = false;

ef_result ef_timer_init(void) {
    if (g_initialized) {
        return EF_ERROR_ALREADY_INITIALIZED;
    }
    g_initialized = true;
    return EF_OK;
}

void ef_timer_shutdown(void) {
    g_initialized = false;
}

ef_time_ns ef_timer_now(void) {
    /* counter * 1e9 would overflow a 64-bit value within about half a
     * second on a 1 GHz counter, so the whole/remainder split below keeps
     * every intermediate product within range for realistic counter
     * frequencies (SDL3's performance counter is well under 10 GHz). */
    const ef_u64 counter = (ef_u64)SDL_GetPerformanceCounter();
    const ef_u64 freq = (ef_u64)SDL_GetPerformanceFrequency();
    const ef_u64 whole_seconds = counter / freq;
    const ef_u64 remainder = counter % freq;
    return (whole_seconds * 1000000000ull) + (remainder * 1000000000ull) / freq;
}

ef_time_ns ef_timer_frequency(void) {
    return (ef_u64)SDL_GetPerformanceFrequency();
}

double ef_timer_ns_to_seconds(ef_time_ns duration) {
    return (double)duration / 1e9;
}

ef_time_ns ef_timer_seconds_to_ns(double seconds) {
    return (ef_time_ns)(seconds * 1e9);
}

double ef_timer_seconds_since(ef_time_ns start) {
    return ef_timer_ns_to_seconds(ef_timer_now() - start);
}

void ef_stopwatch_start(ef_stopwatch *sw) {
    sw->start = ef_timer_now();
}

void ef_stopwatch_reset(ef_stopwatch *sw) {
    sw->start = ef_timer_now();
}

ef_time_ns ef_stopwatch_elapsed_ns(const ef_stopwatch *sw) {
    return ef_timer_now() - sw->start;
}

double ef_stopwatch_elapsed_seconds(const ef_stopwatch *sw) {
    return ef_timer_ns_to_seconds(ef_stopwatch_elapsed_ns(sw));
}

void ef_frame_clock_init(ef_frame_clock *clock) {
    clock->last_tick = ef_timer_now();
    clock->delta_seconds = 0.0;
    clock->time_scale = 1.0;
    clock->frame_count = 0;
}

void ef_frame_clock_tick(ef_frame_clock *clock) {
    const ef_time_ns now = ef_timer_now();
    const ef_time_ns elapsed = now - clock->last_tick;
    clock->last_tick = now;
    clock->delta_seconds = ef_timer_ns_to_seconds(elapsed) * clock->time_scale;
    clock->frame_count += 1;
}

double ef_frame_clock_delta_seconds(const ef_frame_clock *clock) {
    return clock->delta_seconds;
}
