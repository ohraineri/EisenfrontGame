#include "eisenfront/core.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if PLATFORM_WINDOWS
#    include <io.h>
#    define WIN32_LEAN_AND_MEAN
#    include <windows.h>
#    define isatty_stdout() _isatty(1)
#else
#    include <unistd.h>
#    define isatty_stdout() isatty(STDOUT_FILENO)
#endif

#define LOG_MAX_SINKS 8u
#define LOG_MESSAGE_CAPACITY 1024u

typedef struct LogSinkEntry {
    LogSinkId id;
    LogSinkFn fn;
    void     *userdata;
    bool      active;
} LogSinkEntry;

static Mutex        *g_log_mutex = nullptr;
static LogSinkEntry  g_sinks[LOG_MAX_SINKS];
static LogSinkId     g_next_sink_id = 1;
static LogLevel      g_min_level = LOG_LEVEL_INFO;
static bool          g_initialized = false;
static LogSinkId     g_console_sink_id = LOG_INVALID_SINK_ID;
static TimeNs        g_start_time = 0;
static bool          g_console_use_color = false;

static const char *const k_level_strings[LOG_LEVEL_COUNT] = {
    [LOG_LEVEL_TRACE] = "TRACE", [LOG_LEVEL_DEBUG] = "DEBUG", [LOG_LEVEL_INFO] = "INFO",
    [LOG_LEVEL_WARN] = "WARN",   [LOG_LEVEL_ERROR] = "ERROR", [LOG_LEVEL_FATAL] = "FATAL",
};

static const char *const k_level_colors[LOG_LEVEL_COUNT] = {
    [LOG_LEVEL_TRACE] = "\x1b[90m", [LOG_LEVEL_DEBUG] = "\x1b[36m", [LOG_LEVEL_INFO] = "\x1b[37m",
    [LOG_LEVEL_WARN] = "\x1b[33m",  [LOG_LEVEL_ERROR] = "\x1b[31m", [LOG_LEVEL_FATAL] = "\x1b[35m",
};
#define LOG_COLOR_RESET "\x1b[0m"

const char *log_level_to_string(LogLevel level) {
    if ((unsigned)level >= (unsigned)LOG_LEVEL_COUNT) {
        return "UNKNOWN";
    }
    return k_level_strings[level];
}

static void log_console_sink(void *userdata, LogLevel level, const char *category,
                              const char *message) {
    (void)userdata;
    FILE        *stream = (level >= LOG_LEVEL_WARN) ? stderr : stdout;
    const double uptime = time_ns_to_seconds(time_now_ns() - g_start_time);
    if (g_console_use_color) {
        fprintf(stream, "%s[%10.4f] [%-5s] [%s]%s %s\n", k_level_colors[level], uptime,
                k_level_strings[level], category, LOG_COLOR_RESET, message);
    } else {
        fprintf(stream, "[%10.4f] [%-5s] [%s] %s\n", uptime, k_level_strings[level], category,
                message);
    }
}

Result log_init(LogLevel min_level) {
    if (g_initialized) {
        return RESULT_ERROR_ALREADY_INITIALIZED;
    }
    if ((unsigned)min_level >= (unsigned)LOG_LEVEL_COUNT) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const Result mutex_result = mutex_create(&g_log_mutex);
    if (mutex_result != RESULT_OK) {
        return mutex_result;
    }

    memset(g_sinks, 0, sizeof(g_sinks));
    g_next_sink_id = 1;
    g_min_level = min_level;
    g_start_time = time_now_ns();

#if PLATFORM_WINDOWS
    {
        HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD  mode = 0;
        if (handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode)) {
            SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
#endif
    g_console_use_color = isatty_stdout() != 0;
    g_initialized = true;

    const Result sink_result = log_add_sink(log_console_sink, nullptr, &g_console_sink_id);
    if (sink_result != RESULT_OK) {
        g_initialized = false;
        mutex_destroy(g_log_mutex);
        g_log_mutex = nullptr;
        return sink_result;
    }

    return RESULT_OK;
}

void log_shutdown(void) {
    if (!g_initialized) {
        return;
    }
    g_initialized = false;
    memset(g_sinks, 0, sizeof(g_sinks));
    g_console_sink_id = LOG_INVALID_SINK_ID;
    mutex_destroy(g_log_mutex);
    g_log_mutex = nullptr;
}

void log_set_min_level(LogLevel level) {
    if (!g_initialized || (unsigned)level >= (unsigned)LOG_LEVEL_COUNT) {
        return;
    }
    mutex_lock(g_log_mutex);
    g_min_level = level;
    mutex_unlock(g_log_mutex);
}

LogLevel log_get_min_level(void) {
    if (!g_initialized) {
        return g_min_level;
    }
    mutex_lock(g_log_mutex);
    const LogLevel level = g_min_level;
    mutex_unlock(g_log_mutex);
    return level;
}

Result log_add_sink(LogSinkFn sink, void *userdata, LogSinkId *out_id) {
    if (sink == nullptr || !g_initialized) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    mutex_lock(g_log_mutex);
    Result result = RESULT_ERROR_CAPACITY_EXCEEDED;
    for (uint32_t i = 0; i < LOG_MAX_SINKS; ++i) {
        if (!g_sinks[i].active) {
            g_sinks[i].id = g_next_sink_id++;
            g_sinks[i].fn = sink;
            g_sinks[i].userdata = userdata;
            g_sinks[i].active = true;
            if (out_id != nullptr) {
                *out_id = g_sinks[i].id;
            }
            result = RESULT_OK;
            break;
        }
    }
    mutex_unlock(g_log_mutex);
    return result;
}

Result log_remove_sink(LogSinkId id) {
    if (id == LOG_INVALID_SINK_ID || !g_initialized) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    mutex_lock(g_log_mutex);
    Result result = RESULT_ERROR_NOT_FOUND;
    for (uint32_t i = 0; i < LOG_MAX_SINKS; ++i) {
        if (g_sinks[i].active && g_sinks[i].id == id) {
            g_sinks[i] = (LogSinkEntry){0};
            result = RESULT_OK;
            break;
        }
    }
    mutex_unlock(g_log_mutex);
    return result;
}

void log_message(LogLevel level, const char *category, const char *fmt, ...) {
    if (!g_initialized || (unsigned)level >= (unsigned)LOG_LEVEL_COUNT || level < g_min_level) {
        return;
    }
    if (category == nullptr) {
        category = "general";
    }

    char buffer[LOG_MESSAGE_CAPACITY];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    /* Snapshot the sink table under lock, then invoke sinks unlocked so a
     * sink calling back into log_message (or add/remove_sink) cannot
     * deadlock on g_log_mutex. */
    LogSinkEntry snapshot[LOG_MAX_SINKS];
    mutex_lock(g_log_mutex);
    memcpy(snapshot, g_sinks, sizeof(snapshot));
    mutex_unlock(g_log_mutex);

    for (uint32_t i = 0; i < LOG_MAX_SINKS; ++i) {
        if (snapshot[i].active) {
            snapshot[i].fn(snapshot[i].userdata, level, category, buffer);
        }
    }
}
