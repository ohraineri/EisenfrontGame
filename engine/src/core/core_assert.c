#include "eisenfront/core.h"

#include <stdio.h>

static AssertHandlerFn g_assert_handler = nullptr;

void assert_set_handler(AssertHandlerFn handler) {
    g_assert_handler = handler;
}

static void assert_default_handler(const char *expr, const char *message, const char *file,
                                    int line, const char *function) {
    fprintf(stderr, "[ASSERT FAILED] %s:%d in %s()\n    expr: %s\n", file, line, function, expr);
    if (message != nullptr) {
        fprintf(stderr, "    msg:  %s\n", message);
    }
    fflush(stderr);
}

void assert_fail(const char *expr, const char *message, const char *file, int line,
                  const char *function) {
    if (g_assert_handler != nullptr) {
        g_assert_handler(expr, message, file, line, function);
    } else {
        assert_default_handler(expr, message, file, line, function);
    }
}
