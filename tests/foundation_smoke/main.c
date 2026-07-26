#include "eisenfront/foundation/ef_assert.h"
#include "eisenfront/foundation/ef_config.h"
#include "eisenfront/foundation/ef_engine.h"
#include "eisenfront/foundation/ef_log.h"
#include "eisenfront/foundation/ef_timer.h"

#include <stdio.h>

static ef_result demo_module_init(void *userdata) {
    EF_LOG_INFO((const char *)userdata, "demo module initialized");
    return EF_OK;
}

static void demo_module_shutdown(void *userdata) {
    EF_LOG_INFO((const char *)userdata, "demo module shutdown");
}

int main(void) {
    ef_engine_config config = ef_config_default();
    config.app_name = "Foundation Smoke Test";
    config.min_log_level = EF_LOG_LEVEL_TRACE;

    ef_engine *engine = nullptr;
    ef_result  result = ef_engine_create(&config, &engine);
    EF_ASSERT(result == EF_OK);

    const ef_module_desc demo_desc = {
        .name = "demo",
        .userdata = "demo",
        .on_init = demo_module_init,
        .on_shutdown = demo_module_shutdown,
    };
    result = ef_engine_register_module(engine, &demo_desc);
    EF_ASSERT(result == EF_OK);

    result = ef_engine_init(engine);
    EF_ASSERT(result == EF_OK);

    ef_stopwatch sw;
    ef_stopwatch_start(&sw);
    for (volatile int i = 0; i < 1000000; ++i) {
    }
    EF_LOG_INFO("smoke", "busy loop took %.6f seconds", ef_stopwatch_elapsed_seconds(&sw));
    EF_LOG_INFO("smoke", "engine uptime: %.6f seconds", ef_engine_uptime_seconds(engine));

    result = ef_engine_shutdown(engine);
    EF_ASSERT(result == EF_OK);

    ef_engine_destroy(engine);

    printf("foundation smoke test passed\n");
    return 0;
}
