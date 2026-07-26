/*
 * Eisenfront Foundation - engine lifecycle.
 *
 * ef_engine is the single opaque object that owns Foundation's process-wide
 * subsystems (timer, log) plus the module registry. Exactly one may exist
 * at a time: the subsystems it bootstraps are process singletons, so a
 * second concurrent instance would tear down state the first is still
 * using. State machine:
 *
 *   ef_engine_create()  -> EF_ENGINE_STATE_CREATED
 *   ef_engine_init()    -> EF_ENGINE_STATE_RUNNING   (runs module init, LIFO unwind on failure)
 *   ef_engine_shutdown()-> EF_ENGINE_STATE_SHUTDOWN  (runs module shutdown, LIFO)
 *   ef_engine_destroy() -> frees the engine (auto-shuts-down if still RUNNING)
 *
 * Modules may only be registered while CREATED, before any module has been
 * initialized, so registration order is always well defined.
 */
#ifndef EISENFRONT_FOUNDATION_EF_ENGINE_H
#define EISENFRONT_FOUNDATION_EF_ENGINE_H

#include "ef_config.h"
#include "ef_error.h"
#include "ef_module.h"

typedef enum ef_engine_state {
    EF_ENGINE_STATE_CREATED = 0,
    EF_ENGINE_STATE_RUNNING,
    EF_ENGINE_STATE_SHUTDOWN,
} ef_engine_state;

typedef struct ef_engine ef_engine;

/* config may be nullptr to accept ef_config_default() outright. Bootstraps
 * the timer and log subsystems as a side effect. */
ef_result ef_engine_create(const ef_engine_config *config, ef_engine **out_engine);
void      ef_engine_destroy(ef_engine *engine);

ef_result ef_engine_register_module(ef_engine *engine, const ef_module_desc *desc);

ef_result ef_engine_init(ef_engine *engine);
ef_result ef_engine_shutdown(ef_engine *engine);

ef_engine_state         ef_engine_get_state(const ef_engine *engine);
const ef_engine_config *ef_engine_get_config(const ef_engine *engine);
double                  ef_engine_uptime_seconds(const ef_engine *engine);

#endif /* EISENFRONT_FOUNDATION_EF_ENGINE_H */
