#include "eisenfront/core.h"

#include <stdarg.h>
#include <stdio.h>

static thread_local ErrorInfo g_last_error = {
    .code = RESULT_OK,
    .message = {0},
    .file = nullptr,
    .line = 0,
    .function = nullptr,
};

void error_clear(void) {
    g_last_error.code = RESULT_OK;
    g_last_error.message[0] = '\0';
    g_last_error.file = nullptr;
    g_last_error.line = 0;
    g_last_error.function = nullptr;
}

const ErrorInfo *error_get_last(void) {
    return &g_last_error;
}

void error_set_ex(Result code, const char *file, int line, const char *function, const char *fmt,
                   ...) {
    g_last_error.code = code;
    g_last_error.file = file;
    g_last_error.line = line;
    g_last_error.function = function;

    va_list args;
    va_start(args, fmt);
    vsnprintf(g_last_error.message, sizeof(g_last_error.message), fmt, args);
    va_end(args);
}
