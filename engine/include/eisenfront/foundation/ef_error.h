/*
 * Eisenfront Foundation - error handling.
 *
 * Every fallible Foundation function returns an ef_result. EF_OK (zero) is
 * the only success value, so callers can test `if (result)` for failure.
 * For diagnostics beyond a bare code, ef_error_set_ex records a formatted,
 * thread-local message that the caller (or a log sink) can retrieve with
 * ef_error_get_last().
 */
#ifndef EISENFRONT_FOUNDATION_EF_ERROR_H
#define EISENFRONT_FOUNDATION_EF_ERROR_H

#include "ef_platform.h"

typedef enum ef_result {
    EF_OK = 0,
    EF_ERROR_UNKNOWN,
    EF_ERROR_INVALID_ARGUMENT,
    EF_ERROR_OUT_OF_MEMORY,
    EF_ERROR_NOT_INITIALIZED,
    EF_ERROR_ALREADY_INITIALIZED,
    EF_ERROR_NOT_FOUND,
    EF_ERROR_ALREADY_EXISTS,
    EF_ERROR_CAPACITY_EXCEEDED,
    EF_ERROR_INVALID_STATE,
    EF_ERROR_PLATFORM,
    EF_ERROR_MODULE_INIT_FAILED,
    EF_RESULT_COUNT
} ef_result;

const char *ef_result_to_string(ef_result result);

#define EF_ERROR_MESSAGE_CAPACITY 256

typedef struct ef_error_info {
    ef_result   code;
    char        message[EF_ERROR_MESSAGE_CAPACITY];
    const char *file;
    int         line;
    const char *function;
} ef_error_info;

void ef_error_clear(void);

/* Returns a pointer to the calling thread's last recorded error. Never
 * null; code is EF_OK and message is empty if nothing has been recorded. */
const ef_error_info *ef_error_get_last(void);

void ef_error_set_ex(ef_result code, const char *file, int line, const char *function,
                      const char *fmt, ...) EF_PRINTF_FORMAT(5, 6);

#define EF_ERROR_SET(code, ...) ef_error_set_ex((code), __FILE__, __LINE__, __func__, __VA_ARGS__)

#define EF_RETURN_IF_ERROR(expr) \
    do { \
        ef_result const ef_result__ = (expr); \
        if (ef_result__ != EF_OK) { \
            return ef_result__; \
        } \
    } while (0)

#endif /* EISENFRONT_FOUNDATION_EF_ERROR_H */
