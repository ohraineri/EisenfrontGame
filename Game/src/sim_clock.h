/*
 * Fixed-timestep accumulator - Infantry Controller spec 5.1/38.1:
 * movement simulation must run on a fixed simulation timestep, and
 * render-rate variation must not change acceleration, max speed, jump
 * height, stopping distance, stamina use, or rotation speed.
 *
 * Render still happens every real frame; simulation advances in
 * discrete SIM_FIXED_DELTA_SECONDS slices, zero or more per frame,
 * via sim_clock_accumulate() + a sim_clock_consume_step() loop.
 */
#ifndef OUTPOST_SIM_CLOCK_H
#define OUTPOST_SIM_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

#define SIM_FIXED_DELTA_SECONDS (1.0f / 60.0f)

typedef struct SimClock {
    float    accumulator_seconds;
    uint64_t tick_index; /* advances by exactly 1 per consumed fixed tick - never per render frame or catch-up batch */
} SimClock;

void sim_clock_init(SimClock *clock);

/* Adds real elapsed time to the accumulator. Clamped so a large hitch
 * (debugger pause, disk stall) can't queue an unbounded catch-up
 * backlog - the game runs in slow motion through a stall instead of
 * spiraling into simulating an ever-growing backlog. */
void sim_clock_accumulate(SimClock *clock, float real_delta_seconds);

/* Consumes one SIM_FIXED_DELTA_SECONDS slice if enough time has
 * accumulated. Call in a loop until it returns false; each true
 * result means "run one fixed simulation tick now", and advances the
 * monotonic tick counter by exactly one. */
bool sim_clock_consume_step(SimClock *clock);

/* Monotonic count of fixed ticks consumed so far - the authoritative
 * sequence number stamped onto simulation events (e.g.
 * InfantryFootContactEvent), so consumers can order/dedupe them
 * independent of render frame rate. */
uint64_t sim_clock_tick_index(const SimClock *clock);

#endif /* OUTPOST_SIM_CLOCK_H */
