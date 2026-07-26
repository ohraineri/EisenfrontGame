#include "eisenfront/foundation/ef_error.h"

#include <stdarg.h>
#include <stdio.h>

static const char *const k_result_strings[EF_RESULT_COUNT] = {
    [EF_OK] = "OK",
    [EF_ERROR_UNKNOWN] = "Unknown error",
    [EF_ERROR_INVALID_ARGUMENT] = "Invalid argument",
    [EF_ERROR_OUT_OF_MEMORY] = "Out of memory",
    [EF_ERROR_NOT_INITIALIZED] = "Not initialized",
    [EF_ERROR_ALREADY_INITIALIZED] = "Already initialized",
    [EF_ERROR_NOT_FOUND] = "Not found",
    [EF_ERROR_ALREADY_EXISTS] = "Already exists",
    [EF_ERROR_CAPACITY_EXCEEDED] = "Capacity exceeded",
    [EF_ERROR_INVALID_STATE] = "Invalid state",
    [EF_ERROR_PLATFORM] = "Platform error",
    [EF_ERROR_MODULE_INIT_FAILED] = "Module initialization failed",
};

const char *ef_result_to_string(ef_result result) {
    if ((unsigned)result >= (unsigned)EF_RESULT_COUNT) {
        return "Invalid result code";
    }
    return k_result_strings[result];
}

static thread_local ef_error_info g_last_error = {
    .code = EF_OK,
    .message = {0},
    .file = nullptr,
    .line = 0,
    .function = nullptr,
};

void ef_error_clear(void) {
    g_last_error.code = EF_OK;
    g_last_error.message[0] = '\0';
    g_last_error.file = nullptr;
    g_last_error.line = 0;
    g_last_error.function = nullptr;
}

const ef_error_info *ef_error_get_last(void) {
    return &g_last_error;
}

void ef_error_set_ex(ef_result code, const char *file, int line, const char *function,
                      const char *fmt, ...) {
    g_last_error.code = code;
    g_last_error.file = file;
    g_last_error.line = line;
    g_last_error.function = function;

    va_list args;
    va_start(args, fmt);
    vsnprintf(g_last_error.message, sizeof(g_last_error.message), fmt, args);
    va_end(args);
}
