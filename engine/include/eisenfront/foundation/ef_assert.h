/*
 * Eisenfront Foundation - assertions.
 *
 * EF_ASSERT is compiled out entirely when NDEBUG is set (matching
 * EF_BUILD_DEBUG), so it must never be used to guard code with side effects
 * the engine depends on. EF_VERIFY always evaluates its expression and is
 * for invariants that must be checked even in release builds.
 *
 * A failed assertion reports through the pluggable handler and then breaks
 * into the debugger (or terminates the process if none is attached).
 */
#ifndef EISENFRONT_FOUNDATION_EF_ASSERT_H
#define EISENFRONT_FOUNDATION_EF_ASSERT_H

#include "ef_platform.h"

#include <signal.h>

#if EF_PLATFORM_WINDOWS
#    if EF_COMPILER_MSVC
#        define EF_DEBUG_BREAK() __debugbreak()
#    else
#        define EF_DEBUG_BREAK() __builtin_trap()
#    endif
#elif EF_COMPILER_CLANG && defined(__has_builtin)
#    if __has_builtin(__builtin_debugtrap)
#        define EF_DEBUG_BREAK() __builtin_debugtrap()
#    else
#        define EF_DEBUG_BREAK() raise(SIGTRAP)
#    endif
#else
#    define EF_DEBUG_BREAK() raise(SIGTRAP)
#endif

typedef void (*ef_assert_handler_fn)(const char *expr, const char *message, const char *file,
                                      int line, const char *function);

/* Overrides the reporting handler (e.g. to route into the engine log or a
 * crash reporter). Passing nullptr restores the default stderr handler. Not
 * safe to call concurrently with a firing assertion. */
void ef_assert_set_handler(ef_assert_handler_fn handler);

/* Reports a failure through the active handler. Called by the macros below;
 * exposed directly only for the handler-installing translation unit. */
void ef_assert_fail(const char *expr, const char *message, const char *file, int line,
                     const char *function);

#if EF_BUILD_DEBUG
#    define EF_ASSERT(expr) \
        do { \
            if (!(expr)) { \
                ef_assert_fail(#expr, nullptr, __FILE__, __LINE__, __func__); \
                EF_DEBUG_BREAK(); \
            } \
        } while (0)
#    define EF_ASSERT_MSG(expr, msg) \
        do { \
            if (!(expr)) { \
                ef_assert_fail(#expr, (msg), __FILE__, __LINE__, __func__); \
                EF_DEBUG_BREAK(); \
            } \
        } while (0)
#else
#    define EF_ASSERT(expr) ((void)0)
#    define EF_ASSERT_MSG(expr, msg) ((void)0)
#endif

#define EF_VERIFY(expr) \
    do { \
        if (!(expr)) { \
            ef_assert_fail(#expr, nullptr, __FILE__, __LINE__, __func__); \
            EF_DEBUG_BREAK(); \
        } \
    } while (0)

#define EF_STATIC_ASSERT(expr, msg) static_assert(expr, msg)

#endif /* EISENFRONT_FOUNDATION_EF_ASSERT_H */
