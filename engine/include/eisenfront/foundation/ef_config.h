/*
 * Eisenfront Foundation - engine configuration.
 *
 * A plain, validated value type passed once to ef_engine_create. Foundation
 * deliberately does not parse config files; that belongs to a future
 * higher-level module built on top of Platform's filesystem API. Callers
 * start from ef_config_default() and override only the fields they care
 * about (designated initializers make this a single expression).
 */
#ifndef EISENFRONT_FOUNDATION_EF_CONFIG_H
#define EISENFRONT_FOUNDATION_EF_CONFIG_H

#include "ef_error.h"
#include "ef_log.h"
#include "ef_types.h"

typedef struct ef_engine_config {
    const char  *app_name;
    ef_u32       app_version_major;
    ef_u32       app_version_minor;
    ef_u32       app_version_patch;
    ef_log_level min_log_level;
    ef_u32       max_modules;
} ef_engine_config;

ef_engine_config ef_config_default(void);
ef_result        ef_config_validate(const ef_engine_config *config);

#endif /* EISENFRONT_FOUNDATION_EF_CONFIG_H */
