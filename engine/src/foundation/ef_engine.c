#include "eisenfront/foundation/ef_engine.h"

#include "eisenfront/foundation/ef_assert.h"
#include "eisenfront/foundation/ef_log.h"
#include "eisenfront/foundation/ef_timer.h"

#include <stdlib.h>

#define EF_ENGINE_LOG_CATEGORY "engine"

struct ef_engine {
    ef_engine_config    config;
    ef_module_registry *modules;
    ef_engine_state     state;
    ef_time_ns          created_at;
};

/* Foundation's timer and log subsystems are process-wide singletons
 * (see ef_timer.c / ef_log.c); a second concurrently-live engine would
 * tear down state the first is still using, so only one may exist. */
static bool g_engine_exists = false;

ef_result ef_engine_create(const ef_engine_config *config, ef_engine **out_engine) {
    EF_ASSERT(out_engine != nullptr);
    if (out_engine == nullptr) {
        return EF_ERROR_INVALID_ARGUMENT;
    }
    *out_engine = nullptr;

    if (g_engine_exists) {
        return EF_ERROR_ALREADY_INITIALIZED;
    }

    const ef_engine_config effective_config = (config != nullptr) ? *config : ef_config_default();

    ef_result result = ef_config_validate(&effective_config);
    if (result != EF_OK) {
        return result;
    }

    result = ef_timer_init();
    if (result != EF_OK) {
        return result;
    }

    result = ef_log_init(effective_config.min_log_level);
    if (result != EF_OK) {
        ef_timer_shutdown();
        return result;
    }

    ef_engine *engine = malloc(sizeof(*engine));
    if (engine == nullptr) {
        ef_log_shutdown();
        ef_timer_shutdown();
        return EF_ERROR_OUT_OF_MEMORY;
    }

    ef_module_registry *modules = nullptr;
    result = ef_module_registry_create(effective_config.max_modules, &modules);
    if (result != EF_OK) {
        free(engine);
        ef_log_shutdown();
        ef_timer_shutdown();
        return result;
    }

    engine->config = effective_config;
    engine->modules = modules;
    engine->state = EF_ENGINE_STATE_CREATED;
    engine->created_at = ef_timer_now();

    g_engine_exists = true;

    EF_LOG_INFO(EF_ENGINE_LOG_CATEGORY, "Eisenfront engine created for '%s' v%u.%u.%u",
                engine->config.app_name, engine->config.app_version_major,
                engine->config.app_version_minor, engine->config.app_version_patch);

    *out_engine = engine;
    return EF_OK;
}

void ef_engine_destroy(ef_engine *engine) {
    if (engine == nullptr) {
        return;
    }

    if (engine->state == EF_ENGINE_STATE_RUNNING) {
        EF_LOG_WARN(EF_ENGINE_LOG_CATEGORY,
                    "Engine destroyed while still running; forcing shutdown");
        ef_engine_shutdown(engine);
    }

    EF_LOG_INFO(EF_ENGINE_LOG_CATEGORY, "Eisenfront engine destroyed");

    ef_module_registry_destroy(engine->modules);
    free(engine);

    ef_log_shutdown();
    ef_timer_shutdown();
    g_engine_exists = false;
}

ef_result ef_engine_register_module(ef_engine *engine, const ef_module_desc *desc) {
    EF_ASSERT(engine != nullptr);
    if (engine == nullptr) {
        return EF_ERROR_INVALID_ARGUMENT;
    }
    if (engine->state != EF_ENGINE_STATE_CREATED) {
        return EF_ERROR_INVALID_STATE;
    }

    const ef_result result = ef_module_registry_register(engine->modules, desc);
    if (result == EF_OK) {
        EF_LOG_DEBUG(EF_ENGINE_LOG_CATEGORY, "Registered module '%s'", desc->name);
    }
    return result;
}

ef_result ef_engine_init(ef_engine *engine) {
    EF_ASSERT(engine != nullptr);
    if (engine == nullptr) {
        return EF_ERROR_INVALID_ARGUMENT;
    }
    if (engine->state != EF_ENGINE_STATE_CREATED) {
        return EF_ERROR_INVALID_STATE;
    }

    EF_LOG_INFO(EF_ENGINE_LOG_CATEGORY, "Initializing engine (%u modules registered)",
                ef_module_registry_count(engine->modules));

    const ef_result result = ef_module_registry_init_all(engine->modules);
    if (result != EF_OK) {
        EF_LOG_ERROR(EF_ENGINE_LOG_CATEGORY, "Engine initialization failed: %s",
                     ef_result_to_string(result));
        return result;
    }

    engine->state = EF_ENGINE_STATE_RUNNING;
    EF_LOG_INFO(EF_ENGINE_LOG_CATEGORY, "Engine initialized successfully");
    return EF_OK;
}

ef_result ef_engine_shutdown(ef_engine *engine) {
    EF_ASSERT(engine != nullptr);
    if (engine == nullptr) {
        return EF_ERROR_INVALID_ARGUMENT;
    }
    if (engine->state != EF_ENGINE_STATE_RUNNING) {
        return EF_ERROR_INVALID_STATE;
    }

    EF_LOG_INFO(EF_ENGINE_LOG_CATEGORY, "Shutting down engine");
    ef_module_registry_shutdown_all(engine->modules);
    engine->state = EF_ENGINE_STATE_SHUTDOWN;
    EF_LOG_INFO(EF_ENGINE_LOG_CATEGORY, "Engine shutdown complete");
    return EF_OK;
}

ef_engine_state ef_engine_get_state(const ef_engine *engine) {
    EF_ASSERT(engine != nullptr);
    return engine != nullptr ? engine->state : EF_ENGINE_STATE_SHUTDOWN;
}

const ef_engine_config *ef_engine_get_config(const ef_engine *engine) {
    EF_ASSERT(engine != nullptr);
    return engine != nullptr ? &engine->config : nullptr;
}

double ef_engine_uptime_seconds(const ef_engine *engine) {
    EF_ASSERT(engine != nullptr);
    if (engine == nullptr) {
        return 0.0;
    }
    return ef_timer_seconds_since(engine->created_at);
}
