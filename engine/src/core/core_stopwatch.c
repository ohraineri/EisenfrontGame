#include "eisenfront/core.h"

void stopwatch_start(Stopwatch *sw) {
    sw->start = time_now_ns();
}

void stopwatch_reset(Stopwatch *sw) {
    sw->start = time_now_ns();
}

TimeNs stopwatch_elapsed_ns(const Stopwatch *sw) {
    return time_now_ns() - sw->start;
}

double stopwatch_elapsed_seconds(const Stopwatch *sw) {
    return time_ns_to_seconds(stopwatch_elapsed_ns(sw));
}

void frame_clock_init(FrameClock *clock) {
    clock->last_tick = time_now_ns();
    clock->delta_seconds = 0.0;
    clock->time_scale = 1.0;
    clock->frame_count = 0;
}

void frame_clock_tick(FrameClock *clock) {
    const TimeNs now = time_now_ns();
    const TimeNs elapsed = now - clock->last_tick;
    clock->last_tick = now;
    clock->delta_seconds = time_ns_to_seconds(elapsed) * clock->time_scale;
    clock->frame_count += 1;
}

double frame_clock_delta_seconds(const FrameClock *clock) {
    return clock->delta_seconds;
}
