/*
 * Core - engine lifecycle, logging, error handling, assertions, module
 * registration, and frame timing.
 *
 * Dependency: Core depends on Platform (for Time and Mutex) and nothing
 * else in the engine; Platform never depends back on Core. Nothing in
 * this header ever includes SDL3 or any other third-party library -
 * those are scoped to the modules that actually need them (Window,
 * Input, Graphics).
 *
 * Ownership: Engine is the one opaque object Core hands out; it owns the
 * module registry and bootstraps the process-wide log subsystem for the
 * lifetime between engine_create() and engine_destroy(). Only one Engine
 * may exist at a time (see the state-machine note on engine_create).
 */
#ifndef EISENFRONT_CORE_H
#define EISENFRONT_CORE_H

#include "platform.h"

#include <stdlib.h>

/* ==================================================================== *
 * Assertions
 *
 * ASSERT compiles to nothing when NDEBUG is set (BUILD_DEBUG == 0), so it
 * must never guard code with side effects the engine depends on. VERIFY
 * always evaluates its expression and is for invariants that must hold
 * even in release builds. A failed assertion reports through the
 * pluggable handler, then breaks into the debugger (or terminates the
 * process if none is attached).
 * ==================================================================== */

#include <signal.h>

#if PLATFORM_WINDOWS
#    if COMPILER_MSVC
#        define DEBUG_BREAK() __debugbreak()
#    else
#        define DEBUG_BREAK() __builtin_trap()
#    endif
#elif COMPILER_CLANG && defined(__has_builtin)
#    if __has_builtin(__builtin_debugtrap)
#        define DEBUG_BREAK() __builtin_debugtrap()
#    else
#        define DEBUG_BREAK() raise(SIGTRAP)
#    endif
#else
#    define DEBUG_BREAK() raise(SIGTRAP)
#endif

typedef void (*AssertHandlerFn)(const char *expr, const char *message, const char *file, int line,
                                 const char *function);

/* Overrides the reporting handler (e.g. to route into the log or a crash
 * reporter). Passing nullptr restores the default stderr handler. Not
 * safe to call concurrently with a firing assertion. */
ENGINE_API void assert_set_handler(AssertHandlerFn handler);
ENGINE_API void assert_fail(const char *expr, const char *message, const char *file, int line,
                             const char *function);

#if BUILD_DEBUG
#    define ASSERT(expr) \
        do { \
            if (!(expr)) { \
                assert_fail(#expr, nullptr, __FILE__, __LINE__, __func__); \
                DEBUG_BREAK(); \
            } \
        } while (0)
#    define ASSERT_MSG(expr, msg) \
        do { \
            if (!(expr)) { \
                assert_fail(#expr, (msg), __FILE__, __LINE__, __func__); \
                DEBUG_BREAK(); \
            } \
        } while (0)
#else
#    define ASSERT(expr) ((void)0)
#    define ASSERT_MSG(expr, msg) ((void)0)
#endif

#define VERIFY(expr) \
    do { \
        if (!(expr)) { \
            assert_fail(#expr, nullptr, __FILE__, __LINE__, __func__); \
            DEBUG_BREAK(); \
        } \
    } while (0)

/* ==================================================================== *
 * Error handling
 *
 * Result (defined in platform.h) is the return code every fallible
 * function uses; ErrorInfo adds an optional formatted, thread-local
 * message for diagnostics beyond a bare code.
 * ==================================================================== */

#define ERROR_MESSAGE_CAPACITY 256

typedef struct ErrorInfo {
    Result      code;
    char        message[ERROR_MESSAGE_CAPACITY];
    const char *file;
    int         line;
    const char *function;
} ErrorInfo;

ENGINE_API void error_clear(void);

/* Returns the calling thread's last recorded error. Never null; code is
 * RESULT_OK and message is empty if nothing has been recorded. */
ENGINE_API const ErrorInfo *error_get_last(void);

ENGINE_API void error_set_ex(Result code, const char *file, int line, const char *function,
                              const char *fmt, ...) PRINTF_FORMAT(5, 6);

#define ERROR_SET(code, ...) error_set_ex((code), __FILE__, __LINE__, __func__, __VA_ARGS__)

#define RETURN_IF_ERROR(expr) \
    do { \
        Result const result__ = (expr); \
        if (result__ != RESULT_OK) { \
            return result__; \
        } \
    } while (0)

/* ==================================================================== *
 * Logging
 *
 * log_init installs a default colored console sink; additional sinks
 * (file, in-editor console, telemetry) attach with log_add_sink without
 * touching call sites. TRACE/DEBUG macros compile to nothing in release
 * builds so their format arguments are never evaluated outside of debug
 * builds.
 * ==================================================================== */

typedef enum LogLevel {
    LOG_LEVEL_TRACE = 0,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_FATAL,
    LOG_LEVEL_COUNT
} LogLevel;

ENGINE_API const char *log_level_to_string(LogLevel level);

typedef void (*LogSinkFn)(void *userdata, LogLevel level, const char *category,
                           const char *message);

typedef uint32_t LogSinkId;
#define LOG_INVALID_SINK_ID ((LogSinkId)0)

ENGINE_API Result log_init(LogLevel min_level);
ENGINE_API void   log_shutdown(void);

ENGINE_API void     log_set_min_level(LogLevel level);
ENGINE_API LogLevel log_get_min_level(void);

ENGINE_API Result log_add_sink(LogSinkFn sink, void *userdata, LogSinkId *out_id);
ENGINE_API Result log_remove_sink(LogSinkId id);

ENGINE_API void log_message(LogLevel level, const char *category, const char *fmt, ...)
    PRINTF_FORMAT(3, 4);

#if BUILD_DEBUG
#    define LOG_TRACE(category, ...) log_message(LOG_LEVEL_TRACE, (category), __VA_ARGS__)
#    define LOG_DEBUG(category, ...) log_message(LOG_LEVEL_DEBUG, (category), __VA_ARGS__)
#else
#    define LOG_TRACE(category, ...) ((void)0)
#    define LOG_DEBUG(category, ...) ((void)0)
#endif

#define LOG_INFO(category, ...) log_message(LOG_LEVEL_INFO, (category), __VA_ARGS__)
#define LOG_WARN(category, ...) log_message(LOG_LEVEL_WARN, (category), __VA_ARGS__)
#define LOG_ERROR(category, ...) log_message(LOG_LEVEL_ERROR, (category), __VA_ARGS__)

/* Logs at FATAL, flushes and tears down the logger, then aborts. Reserved
 * for truly unrecoverable states inside Core itself; higher layers define
 * their own recovery policy rather than using this. */
#define LOG_FATAL(category, ...) \
    do { \
        log_message(LOG_LEVEL_FATAL, (category), __VA_ARGS__); \
        log_shutdown(); \
        abort(); \
    } while (0)

/* ==================================================================== *
 * Frame timing
 *
 * Built on Platform's raw TimeNs/time_now_ns; Stopwatch measures a single
 * elapsed interval, FrameClock is the per-frame delta-time helper the
 * engine loop drives every tick.
 * ==================================================================== */

typedef struct Stopwatch {
    TimeNs start;
} Stopwatch;

ENGINE_API void   stopwatch_start(Stopwatch *sw);
ENGINE_API void   stopwatch_reset(Stopwatch *sw);
ENGINE_API TimeNs stopwatch_elapsed_ns(const Stopwatch *sw);
ENGINE_API double stopwatch_elapsed_seconds(const Stopwatch *sw);

typedef struct FrameClock {
    TimeNs last_tick;
    double delta_seconds;
    double time_scale;
    uint64_t frame_count;
} FrameClock;

ENGINE_API void   frame_clock_init(FrameClock *clock);
ENGINE_API void   frame_clock_tick(FrameClock *clock);
ENGINE_API double frame_clock_delta_seconds(const FrameClock *clock);

/* ==================================================================== *
 * Module registration
 *
 * A module is a name plus two optional callbacks; the registry only
 * sequences them; it has no opinion on what a module does. The engine
 * calls module_registry_init_all() once, in registration order; if any
 * on_init fails, everything already started is unwound in reverse order
 * (LIFO) before the error is returned. Shutdown always runs LIFO so a
 * module never outlives one it depends on.
 * ==================================================================== */

typedef Result (*ModuleInitFn)(void *userdata);
typedef void (*ModuleShutdownFn)(void *userdata);

typedef struct ModuleDesc {
    const char      *name;
    void             *userdata;
    ModuleInitFn      on_init;     /* may be nullptr */
    ModuleShutdownFn  on_shutdown; /* may be nullptr */
} ModuleDesc;

typedef struct ModuleRegistry ModuleRegistry;

ENGINE_API Result module_registry_create(uint32_t capacity, ModuleRegistry **out_registry);
ENGINE_API void   module_registry_destroy(ModuleRegistry *registry);

ENGINE_API Result module_registry_register(ModuleRegistry *registry, const ModuleDesc *desc);

ENGINE_API Result module_registry_init_all(ModuleRegistry *registry);
ENGINE_API void   module_registry_shutdown_all(ModuleRegistry *registry);

ENGINE_API uint32_t    module_registry_count(const ModuleRegistry *registry);
ENGINE_API const char *module_registry_name_at(const ModuleRegistry *registry, uint32_t index);

/* ==================================================================== *
 * Engine lifecycle
 *
 * State machine:
 *   engine_create()   -> ENGINE_STATE_CREATED
 *   engine_init()     -> ENGINE_STATE_RUNNING    (runs module init, LIFO unwind on failure)
 *   engine_shutdown() -> ENGINE_STATE_SHUTDOWN   (runs module shutdown, LIFO)
 *   engine_destroy()  -> frees the engine (auto-shuts-down if still RUNNING)
 *
 * Modules may only be registered while CREATED, before any module has
 * been initialized, so registration order is always well defined.
 * ==================================================================== */

typedef struct EngineConfig {
    const char *app_name;
    uint32_t    app_version_major;
    uint32_t    app_version_minor;
    uint32_t    app_version_patch;
    LogLevel    min_log_level;
    uint32_t    max_modules;
} EngineConfig;

ENGINE_API EngineConfig engine_config_default(void);
ENGINE_API Result       engine_config_validate(const EngineConfig *config);

typedef enum EngineState {
    ENGINE_STATE_CREATED = 0,
    ENGINE_STATE_RUNNING,
    ENGINE_STATE_SHUTDOWN,
} EngineState;

typedef struct Engine Engine;

/* config may be nullptr to accept engine_config_default() outright.
 * Bootstraps the log subsystem as a side effect. */
ENGINE_API Result engine_create(const EngineConfig *config, Engine **out_engine);
ENGINE_API void   engine_destroy(Engine *engine);

ENGINE_API Result engine_register_module(Engine *engine, const ModuleDesc *desc);

ENGINE_API Result engine_init(Engine *engine);
ENGINE_API Result engine_shutdown(Engine *engine);

ENGINE_API EngineState         engine_get_state(const Engine *engine);
ENGINE_API const EngineConfig *engine_get_config(const Engine *engine);
ENGINE_API double               engine_uptime_seconds(const Engine *engine);

#endif /* EISENFRONT_CORE_H */
