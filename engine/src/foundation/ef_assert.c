#include "eisenfront/foundation/ef_assert.h"

#include <stdio.h>

static ef_assert_handler_fn g_assert_handler = nullptr;

void ef_assert_set_handler(ef_assert_handler_fn handler) {
    g_assert_handler = handler;
}

static void ef_assert_default_handler(const char *expr, const char *message, const char *file,
                                       int line, const char *function) {
    fprintf(stderr, "[ASSERT FAILED] %s:%d in %s()\n    expr: %s\n", file, line, function, expr);
    if (message != nullptr) {
        fprintf(stderr, "    msg:  %s\n", message);
    }
    fflush(stderr);
}

void ef_assert_fail(const char *expr, const char *message, const char *file, int line,
                     const char *function) {
    if (g_assert_handler != nullptr) {
        g_assert_handler(expr, message, file, line, function);
    } else {
        ef_assert_default_handler(expr, message, file, line, function);
    }
}
