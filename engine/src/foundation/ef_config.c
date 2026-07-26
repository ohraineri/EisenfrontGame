#include "eisenfront/foundation/ef_config.h"

#include "eisenfront/foundation/ef_assert.h"

#include <string.h>

ef_engine_config ef_config_default(void) {
    return (ef_engine_config){
        .app_name = "Eisenfront Application",
        .app_version_major = 0,
        .app_version_minor = 1,
        .app_version_patch = 0,
        .min_log_level = EF_LOG_LEVEL_INFO,
        .max_modules = 32,
    };
}

ef_result ef_config_validate(const ef_engine_config *config) {
    EF_ASSERT(config != nullptr);
    if (config == nullptr) {
        return EF_ERROR_INVALID_ARGUMENT;
    }
    if (config->app_name == nullptr || config->app_name[0] == '\0') {
        return EF_ERROR_INVALID_ARGUMENT;
    }
    if (strlen(config->app_name) >= 128) {
        return EF_ERROR_INVALID_ARGUMENT;
    }
    if ((unsigned)config->min_log_level >= (unsigned)EF_LOG_LEVEL_COUNT) {
        return EF_ERROR_INVALID_ARGUMENT;
    }
    if (config->max_modules == 0 || config->max_modules > 256) {
        return EF_ERROR_INVALID_ARGUMENT;
    }
    return EF_OK;
}
