/*
 * sim_clock is pure math/logic (no engine dependency at all) - these
 * tests verify the fixed-timestep accumulator directly.
 */
#include "sim_clock.h"

#include "unity.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_no_step_before_enough_time_accumulates(void) {
    SimClock clock;
    sim_clock_init(&clock);

    sim_clock_accumulate(&clock, SIM_FIXED_DELTA_SECONDS * 0.5f);
    TEST_ASSERT_FALSE(sim_clock_consume_step(&clock));
}

static void test_one_step_per_fixed_delta_accumulated(void) {
    SimClock clock;
    sim_clock_init(&clock);

    sim_clock_accumulate(&clock, SIM_FIXED_DELTA_SECONDS);
    TEST_ASSERT_TRUE(sim_clock_consume_step(&clock));
    TEST_ASSERT_FALSE(sim_clock_consume_step(&clock));
}

static void test_leftover_time_is_not_lost_across_frames(void) {
    SimClock clock;
    sim_clock_init(&clock);

    /* Two frames of half a fixed step each (as a 120Hz host would
     * produce against a 60Hz sim) must add up to exactly one step,
     * not zero - leftover accumulator time must carry over. */
    sim_clock_accumulate(&clock, SIM_FIXED_DELTA_SECONDS * 0.5f);
    TEST_ASSERT_FALSE(sim_clock_consume_step(&clock));
    sim_clock_accumulate(&clock, SIM_FIXED_DELTA_SECONDS * 0.5f);
    TEST_ASSERT_TRUE(sim_clock_consume_step(&clock));
    TEST_ASSERT_FALSE(sim_clock_consume_step(&clock));
}

static void test_large_hitch_clamps_catch_up_backlog(void) {
    SimClock clock;
    sim_clock_init(&clock);

    /* A 10-second stall must not queue ~600 fixed steps of catch-up -
     * the accumulator clamps to a bounded backlog instead. */
    sim_clock_accumulate(&clock, 10.0f);

    int steps = 0;
    while (sim_clock_consume_step(&clock)) {
        steps += 1;
    }
    TEST_ASSERT_LESS_OR_EQUAL(16, steps);
    TEST_ASSERT_GREATER_THAN(0, steps);
}

static void test_step_count_is_frame_rate_independent(void) {
    /* One simulated second's worth of fixed steps must be identical
     * whether the host renders at 30fps or 240fps - the actual
     * invariant that makes gameplay deterministic across frame rates
     * (Infantry Controller spec 5.1/38.1). */
    SimClock clock_30fps;
    sim_clock_init(&clock_30fps);
    int steps_30fps = 0;
    for (int frame = 0; frame < 30; ++frame) {
        sim_clock_accumulate(&clock_30fps, 1.0f / 30.0f);
        while (sim_clock_consume_step(&clock_30fps)) {
            steps_30fps += 1;
        }
    }

    SimClock clock_240fps;
    sim_clock_init(&clock_240fps);
    int steps_240fps = 0;
    for (int frame = 0; frame < 240; ++frame) {
        sim_clock_accumulate(&clock_240fps, 1.0f / 240.0f);
        while (sim_clock_consume_step(&clock_240fps)) {
            steps_240fps += 1;
        }
    }

    /* Within 1 step, not bit-exact: summing many small floats (1/240s
     * each) vs fewer larger ones (1/30s each) accumulates different
     * float rounding error, which can shift which side of a step
     * boundary the last partial tick lands on - that's a float
     * precision artifact, not a frame-rate dependency bug. */
    TEST_ASSERT_INT_WITHIN(1, steps_30fps, steps_240fps);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_no_step_before_enough_time_accumulates);
    RUN_TEST(test_one_step_per_fixed_delta_accumulated);
    RUN_TEST(test_leftover_time_is_not_lost_across_frames);
    RUN_TEST(test_large_hitch_clamps_catch_up_backlog);
    RUN_TEST(test_step_count_is_frame_rate_independent);

    return UNITY_END();
}
