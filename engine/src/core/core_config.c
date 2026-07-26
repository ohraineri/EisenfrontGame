#include "eisenfront/core.h"

#include <string.h>

EngineConfig engine_config_default(void) {
    return (EngineConfig){
        .app_name = "Eisenfront Application",
        .app_version_major = 0,
        .app_version_minor = 1,
        .app_version_patch = 0,
        .min_log_level = LOG_LEVEL_INFO,
        .max_modules = 32,
    };
}

Result engine_config_validate(const EngineConfig *config) {
    ASSERT(config != nullptr);
    if (config == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (config->app_name == nullptr || config->app_name[0] == '\0') {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (strlen(config->app_name) >= 128) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if ((unsigned)config->min_log_level >= (unsigned)LOG_LEVEL_COUNT) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (config->max_modules == 0 || config->max_modules > 256) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    return RESULT_OK;
}
