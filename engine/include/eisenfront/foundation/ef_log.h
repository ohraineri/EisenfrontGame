/*
 * Eisenfront Foundation - logging.
 *
 * ef_log_init installs a default colored console sink; additional sinks
 * (file, in-editor console, telemetry) can be attached with ef_log_add_sink
 * without touching call sites. TRACE/DEBUG macros compile to nothing in
 * release builds (EF_BUILD_DEBUG == 0) so their format arguments are never
 * evaluated outside of debug builds.
 */
#ifndef EISENFRONT_FOUNDATION_EF_LOG_H
#define EISENFRONT_FOUNDATION_EF_LOG_H

#include "ef_error.h"
#include "ef_platform.h"
#include "ef_types.h"

#include <stdlib.h>

typedef enum ef_log_level {
    EF_LOG_LEVEL_TRACE = 0,
    EF_LOG_LEVEL_DEBUG,
    EF_LOG_LEVEL_INFO,
    EF_LOG_LEVEL_WARN,
    EF_LOG_LEVEL_ERROR,
    EF_LOG_LEVEL_FATAL,
    EF_LOG_LEVEL_COUNT
} ef_log_level;

const char *ef_log_level_to_string(ef_log_level level);

typedef void (*ef_log_sink_fn)(void *userdata, ef_log_level level, const char *category,
                                const char *message);

typedef ef_u32 ef_log_sink_id;
#define EF_LOG_INVALID_SINK_ID ((ef_log_sink_id)0)

ef_result ef_log_init(ef_log_level min_level);
void      ef_log_shutdown(void);

void         ef_log_set_min_level(ef_log_level level);
ef_log_level ef_log_get_min_level(void);

ef_result ef_log_add_sink(ef_log_sink_fn sink, void *userdata, ef_log_sink_id *out_id);
ef_result ef_log_remove_sink(ef_log_sink_id id);

void ef_log_message(ef_log_level level, const char *category, const char *fmt, ...)
    EF_PRINTF_FORMAT(3, 4);

#if EF_BUILD_DEBUG
#    define EF_LOG_TRACE(category, ...) ef_log_message(EF_LOG_LEVEL_TRACE, (category), __VA_ARGS__)
#    define EF_LOG_DEBUG(category, ...) ef_log_message(EF_LOG_LEVEL_DEBUG, (category), __VA_ARGS__)
#else
#    define EF_LOG_TRACE(category, ...) ((void)0)
#    define EF_LOG_DEBUG(category, ...) ((void)0)
#endif

#define EF_LOG_INFO(category, ...) ef_log_message(EF_LOG_LEVEL_INFO, (category), __VA_ARGS__)
#define EF_LOG_WARN(category, ...) ef_log_message(EF_LOG_LEVEL_WARN, (category), __VA_ARGS__)
#define EF_LOG_ERROR(category, ...) ef_log_message(EF_LOG_LEVEL_ERROR, (category), __VA_ARGS__)

/* Logs at FATAL, flushes and tears down the logger, then aborts. Foundation
 * has no concept of a recoverable engine-fatal condition; that policy
 * belongs to higher layers, so this macro is reserved for truly
 * unrecoverable states inside Foundation itself. */
#define EF_LOG_FATAL(category, ...) \
    do { \
        ef_log_message(EF_LOG_LEVEL_FATAL, (category), __VA_ARGS__); \
        ef_log_shutdown(); \
        abort(); \
    } while (0)

#endif /* EISENFRONT_FOUNDATION_EF_LOG_H */
