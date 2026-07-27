#include "sim_clock.h"

#define SIM_CLOCK_MAX_ACCUMULATOR_SECONDS 0.25f

void sim_clock_init(SimClock *clock) {
    clock->accumulator_seconds = 0.0f;
    clock->tick_index = 0;
}

void sim_clock_accumulate(SimClock *clock, float real_delta_seconds) {
    clock->accumulator_seconds += real_delta_seconds;
    if (clock->accumulator_seconds > SIM_CLOCK_MAX_ACCUMULATOR_SECONDS) {
        clock->accumulator_seconds = SIM_CLOCK_MAX_ACCUMULATOR_SECONDS;
    }
}

bool sim_clock_consume_step(SimClock *clock) {
    if (clock->accumulator_seconds < SIM_FIXED_DELTA_SECONDS) {
        return false;
    }
    clock->accumulator_seconds -= SIM_FIXED_DELTA_SECONDS;
    clock->tick_index += 1;
    return true;
}

uint64_t sim_clock_tick_index(const SimClock *clock) {
    return clock->tick_index;
}
