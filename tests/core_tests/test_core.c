#include "eisenfront/core.h"

#include "unity.h"

#include <stdio.h>
#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

/* ---- Config -------------------------------------------------------------- */

static void test_config_default_is_valid(void) {
    const EngineConfig config = engine_config_default();
    TEST_ASSERT_EQUAL(RESULT_OK, engine_config_validate(&config));
}

static void test_config_validate_rejects_empty_name(void) {
    EngineConfig config = engine_config_default();
    config.app_name = "";
    TEST_ASSERT_EQUAL(RESULT_ERROR_INVALID_ARGUMENT, engine_config_validate(&config));
}

static void test_config_validate_rejects_zero_modules(void) {
    EngineConfig config = engine_config_default();
    config.max_modules = 0;
    TEST_ASSERT_EQUAL(RESULT_ERROR_INVALID_ARGUMENT, engine_config_validate(&config));
}

/* ---- Engine lifecycle ------------------------------------------------------ */

static int  g_init_order[4];
static int  g_init_count;
static int  g_shutdown_order[4];
static int  g_shutdown_count;

static void reset_order_tracking(void) {
    g_init_count = 0;
    g_shutdown_count = 0;
}

static Result module_a_init(void *userdata) {
    (void)userdata;
    g_init_order[g_init_count++] = 0;
    return RESULT_OK;
}
static void module_a_shutdown(void *userdata) {
    (void)userdata;
    g_shutdown_order[g_shutdown_count++] = 0;
}
static Result module_b_init(void *userdata) {
    (void)userdata;
    g_init_order[g_init_count++] = 1;
    return RESULT_OK;
}
static void module_b_shutdown(void *userdata) {
    (void)userdata;
    g_shutdown_order[g_shutdown_count++] = 1;
}

static void test_engine_lifecycle_runs_modules_in_order_and_shuts_down_lifo(void) {
    reset_order_tracking();

    EngineConfig config = engine_config_default();
    config.app_name = "core_tests";
    config.min_log_level = LOG_LEVEL_FATAL; /* keep test output quiet */

    Engine *engine = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, engine_create(&config, &engine));
    TEST_ASSERT_EQUAL(ENGINE_STATE_CREATED, engine_get_state(engine));

    const ModuleDesc a = {.name = "a", .on_init = module_a_init, .on_shutdown = module_a_shutdown};
    const ModuleDesc b = {.name = "b", .on_init = module_b_init, .on_shutdown = module_b_shutdown};
    TEST_ASSERT_EQUAL(RESULT_OK, engine_register_module(engine, &a));
    TEST_ASSERT_EQUAL(RESULT_OK, engine_register_module(engine, &b));

    TEST_ASSERT_EQUAL(RESULT_OK, engine_init(engine));
    TEST_ASSERT_EQUAL(ENGINE_STATE_RUNNING, engine_get_state(engine));
    TEST_ASSERT_EQUAL(2, g_init_count);
    TEST_ASSERT_EQUAL(0, g_init_order[0]); /* a before b */
    TEST_ASSERT_EQUAL(1, g_init_order[1]);

    TEST_ASSERT_EQUAL(RESULT_OK, engine_shutdown(engine));
    TEST_ASSERT_EQUAL(ENGINE_STATE_SHUTDOWN, engine_get_state(engine));
    TEST_ASSERT_EQUAL(2, g_shutdown_count);
    TEST_ASSERT_EQUAL(1, g_shutdown_order[0]); /* b before a: LIFO */
    TEST_ASSERT_EQUAL(0, g_shutdown_order[1]);

    engine_destroy(engine);
}

static void test_engine_register_module_rejected_after_init(void) {
    EngineConfig config = engine_config_default();
    config.app_name = "core_tests";
    config.min_log_level = LOG_LEVEL_FATAL;

    Engine *engine = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, engine_create(&config, &engine));
    TEST_ASSERT_EQUAL(RESULT_OK, engine_init(engine));

    const ModuleDesc late = {.name = "late"};
    TEST_ASSERT_EQUAL(RESULT_ERROR_INVALID_STATE, engine_register_module(engine, &late));

    engine_shutdown(engine);
    engine_destroy(engine);
}

static Result failing_module_init(void *userdata) {
    (void)userdata;
    return RESULT_ERROR_MODULE_INIT_FAILED;
}

static void test_engine_init_rolls_back_on_module_failure(void) {
    reset_order_tracking();

    EngineConfig config = engine_config_default();
    config.app_name = "core_tests";
    config.min_log_level = LOG_LEVEL_FATAL;

    Engine *engine = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, engine_create(&config, &engine));

    const ModuleDesc ok_module = {
        .name = "ok", .on_init = module_a_init, .on_shutdown = module_a_shutdown};
    const ModuleDesc bad_module = {.name = "bad", .on_init = failing_module_init};
    TEST_ASSERT_EQUAL(RESULT_OK, engine_register_module(engine, &ok_module));
    TEST_ASSERT_EQUAL(RESULT_OK, engine_register_module(engine, &bad_module));

    TEST_ASSERT_EQUAL(RESULT_ERROR_MODULE_INIT_FAILED, engine_init(engine));
    /* the already-started "ok" module must have been rolled back */
    TEST_ASSERT_EQUAL(1, g_init_count);
    TEST_ASSERT_EQUAL(1, g_shutdown_count);
    TEST_ASSERT_EQUAL(ENGINE_STATE_CREATED, engine_get_state(engine));

    engine_destroy(engine);
}

static void test_engine_destroy_while_running_forces_shutdown(void) {
    reset_order_tracking();

    EngineConfig config = engine_config_default();
    config.app_name = "core_tests";
    config.min_log_level = LOG_LEVEL_FATAL;

    Engine *engine = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, engine_create(&config, &engine));
    const ModuleDesc a = {.name = "a", .on_init = module_a_init, .on_shutdown = module_a_shutdown};
    TEST_ASSERT_EQUAL(RESULT_OK, engine_register_module(engine, &a));
    TEST_ASSERT_EQUAL(RESULT_OK, engine_init(engine));

    engine_destroy(engine); /* must not leak / must call module_a_shutdown */
    TEST_ASSERT_EQUAL(1, g_shutdown_count);
}

/* ---- Logging --------------------------------------------------------------- */

static char g_captured_message[256];
static int  g_captured_count;

static void capture_sink(void *userdata, LogLevel level, const char *category,
                          const char *message) {
    (void)userdata;
    (void)level;
    (void)category;
    g_captured_count += 1;
    snprintf(g_captured_message, sizeof(g_captured_message), "%s", message);
}

static void test_log_respects_min_level_and_delivers_to_sinks(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, log_init(LOG_LEVEL_WARN));

    g_captured_count = 0;
    LogSinkId sink_id = LOG_INVALID_SINK_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, log_add_sink(capture_sink, nullptr, &sink_id));

    LOG_INFO("test", "this is below the WARN threshold");
    TEST_ASSERT_EQUAL(0, g_captured_count);

    log_message(LOG_LEVEL_ERROR, "test", "error message %d", 42);
    TEST_ASSERT_EQUAL(1, g_captured_count);
    TEST_ASSERT_EQUAL_STRING("error message 42", g_captured_message);

    TEST_ASSERT_EQUAL(RESULT_OK, log_remove_sink(sink_id));
    log_shutdown();
}

/* ---- Frame timing ----------------------------------------------------------- */

static void test_stopwatch_measures_elapsed_time(void) {
    Stopwatch sw;
    stopwatch_start(&sw);
    sleep_ms(5);
    TEST_ASSERT_TRUE(stopwatch_elapsed_seconds(&sw) > 0.0);
}

static void test_frame_clock_delta_reflects_time_scale(void) {
    FrameClock clock;
    frame_clock_init(&clock);
    clock.time_scale = 0.0;
    sleep_ms(5);
    frame_clock_tick(&clock);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, frame_clock_delta_seconds(&clock));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_config_default_is_valid);
    RUN_TEST(test_config_validate_rejects_empty_name);
    RUN_TEST(test_config_validate_rejects_zero_modules);
    RUN_TEST(test_engine_lifecycle_runs_modules_in_order_and_shuts_down_lifo);
    RUN_TEST(test_engine_register_module_rejected_after_init);
    RUN_TEST(test_engine_init_rolls_back_on_module_failure);
    RUN_TEST(test_engine_destroy_while_running_forces_shutdown);
    RUN_TEST(test_log_respects_min_level_and_delivers_to_sinks);
    RUN_TEST(test_stopwatch_measures_elapsed_time);
    RUN_TEST(test_frame_clock_delta_reflects_time_scale);

    return UNITY_END();
}
