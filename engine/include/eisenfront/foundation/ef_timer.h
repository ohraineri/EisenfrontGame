/*
 * Eisenfront Foundation - high resolution timer.
 *
 * Wraps a monotonic performance counter (via SDL3) with correct, overflow
 * safe conversion to nanoseconds. Foundation is the only layer that touches
 * SDL3 for timing; nothing SDL-specific is exposed here. ef_stopwatch
 * measures a single elapsed interval; ef_frame_clock is the per-frame delta
 * time helper the engine loop drives every tick.
 */
#ifndef EISENFRONT_FOUNDATION_EF_TIMER_H
#define EISENFRONT_FOUNDATION_EF_TIMER_H

#include "ef_error.h"
#include "ef_types.h"

typedef ef_u64 ef_time_ns;

ef_result ef_timer_init(void);
void      ef_timer_shutdown(void);

/* Nanoseconds since an unspecified, monotonic starting point. Only valid
 * for computing differences against other ef_timer_now() calls. */
ef_time_ns ef_timer_now(void);
ef_time_ns ef_timer_frequency(void);

double     ef_timer_ns_to_seconds(ef_time_ns duration);
ef_time_ns ef_timer_seconds_to_ns(double seconds);
double     ef_timer_seconds_since(ef_time_ns start);

typedef struct ef_stopwatch {
    ef_time_ns start;
} ef_stopwatch;

void       ef_stopwatch_start(ef_stopwatch *sw);
void       ef_stopwatch_reset(ef_stopwatch *sw);
ef_time_ns ef_stopwatch_elapsed_ns(const ef_stopwatch *sw);
double     ef_stopwatch_elapsed_seconds(const ef_stopwatch *sw);

typedef struct ef_frame_clock {
    ef_time_ns last_tick;
    double     delta_seconds;
    double     time_scale;
    ef_u64     frame_count;
} ef_frame_clock;

void   ef_frame_clock_init(ef_frame_clock *clock);
void   ef_frame_clock_tick(ef_frame_clock *clock);
double ef_frame_clock_delta_seconds(const ef_frame_clock *clock);

#endif /* EISENFRONT_FOUNDATION_EF_TIMER_H */
