#include "eisenfront/core.h"

#include <stdlib.h>

#define ENGINE_LOG_CATEGORY "engine"

struct Engine {
    EngineConfig    config;
    ModuleRegistry *modules;
    EngineState     state;
    TimeNs          created_at;
};

/* Core's log subsystem is a process-wide singleton (see core_log.c); a
 * second concurrently-live engine would tear down state the first is
 * still using, so only one may exist. */
static bool g_engine_exists = false;

Result engine_create(const EngineConfig *config, Engine **out_engine) {
    ASSERT(out_engine != nullptr);
    if (out_engine == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    *out_engine = nullptr;

    if (g_engine_exists) {
        return RESULT_ERROR_ALREADY_INITIALIZED;
    }

    const EngineConfig effective_config = (config != nullptr) ? *config : engine_config_default();

    Result result = engine_config_validate(&effective_config);
    if (result != RESULT_OK) {
        return result;
    }

    result = log_init(effective_config.min_log_level);
    if (result != RESULT_OK) {
        return result;
    }

    Engine *engine = malloc(sizeof(*engine));
    if (engine == nullptr) {
        log_shutdown();
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    ModuleRegistry *modules = nullptr;
    result = module_registry_create(effective_config.max_modules, &modules);
    if (result != RESULT_OK) {
        free(engine);
        log_shutdown();
        return result;
    }

    engine->config = effective_config;
    engine->modules = modules;
    engine->state = ENGINE_STATE_CREATED;
    engine->created_at = time_now_ns();

    g_engine_exists = true;

    LOG_INFO(ENGINE_LOG_CATEGORY, "Eisenfront engine created for '%s' v%u.%u.%u",
             engine->config.app_name, engine->config.app_version_major,
             engine->config.app_version_minor, engine->config.app_version_patch);

    *out_engine = engine;
    return RESULT_OK;
}

void engine_destroy(Engine *engine) {
    if (engine == nullptr) {
        return;
    }

    if (engine->state == ENGINE_STATE_RUNNING) {
        LOG_WARN(ENGINE_LOG_CATEGORY, "Engine destroyed while still running; forcing shutdown");
        engine_shutdown(engine);
    }

    LOG_INFO(ENGINE_LOG_CATEGORY, "Eisenfront engine destroyed");

    module_registry_destroy(engine->modules);
    free(engine);

    log_shutdown();
    g_engine_exists = false;
}

Result engine_register_module(Engine *engine, const ModuleDesc *desc) {
    ASSERT(engine != nullptr);
    if (engine == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (engine->state != ENGINE_STATE_CREATED) {
        return RESULT_ERROR_INVALID_STATE;
    }

    const Result result = module_registry_register(engine->modules, desc);
    if (result == RESULT_OK) {
        LOG_DEBUG(ENGINE_LOG_CATEGORY, "Registered module '%s'", desc->name);
    }
    return result;
}

Result engine_init(Engine *engine) {
    ASSERT(engine != nullptr);
    if (engine == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (engine->state != ENGINE_STATE_CREATED) {
        return RESULT_ERROR_INVALID_STATE;
    }

    LOG_INFO(ENGINE_LOG_CATEGORY, "Initializing engine (%u modules registered)",
             module_registry_count(engine->modules));

    const Result result = module_registry_init_all(engine->modules);
    if (result != RESULT_OK) {
        LOG_ERROR(ENGINE_LOG_CATEGORY, "Engine initialization failed: %s",
                  result_to_string(result));
        return result;
    }

    engine->state = ENGINE_STATE_RUNNING;
    LOG_INFO(ENGINE_LOG_CATEGORY, "Engine initialized successfully");
    return RESULT_OK;
}

Result engine_shutdown(Engine *engine) {
    ASSERT(engine != nullptr);
    if (engine == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (engine->state != ENGINE_STATE_RUNNING) {
        return RESULT_ERROR_INVALID_STATE;
    }

    LOG_INFO(ENGINE_LOG_CATEGORY, "Shutting down engine");
    module_registry_shutdown_all(engine->modules);
    engine->state = ENGINE_STATE_SHUTDOWN;
    LOG_INFO(ENGINE_LOG_CATEGORY, "Engine shutdown complete");
    return RESULT_OK;
}

EngineState engine_get_state(const Engine *engine) {
    ASSERT(engine != nullptr);
    return engine != nullptr ? engine->state : ENGINE_STATE_SHUTDOWN;
}

const EngineConfig *engine_get_config(const Engine *engine) {
    ASSERT(engine != nullptr);
    return engine != nullptr ? &engine->config : nullptr;
}

double engine_uptime_seconds(const Engine *engine) {
    ASSERT(engine != nullptr);
    if (engine == nullptr) {
        return 0.0;
    }
    return time_ns_to_seconds(time_now_ns() - engine->created_at);
}
