#include "eisenfront/foundation/ef_log.h"

#include "eisenfront/foundation/ef_timer.h"

#include <SDL3/SDL_mutex.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if EF_PLATFORM_WINDOWS
#    include <io.h>
#    include <windows.h>
#    define ef_isatty(fd) _isatty(fd)
#    define EF_STDOUT_FILENO 1
#else
#    include <unistd.h>
#    define ef_isatty(fd) isatty(fd)
#    define EF_STDOUT_FILENO STDOUT_FILENO
#endif

#define EF_LOG_MAX_SINKS 8u
#define EF_LOG_MESSAGE_CAPACITY 1024u

typedef struct ef_log_sink_entry {
    ef_log_sink_id id;
    ef_log_sink_fn fn;
    void          *userdata;
    bool           active;
} ef_log_sink_entry;

static SDL_Mutex        *g_log_mutex = nullptr;
static ef_log_sink_entry g_sinks[EF_LOG_MAX_SINKS];
static ef_log_sink_id    g_next_sink_id = 1;
static ef_log_level      g_min_level = EF_LOG_LEVEL_INFO;
static bool              g_initialized = false;
static ef_log_sink_id    g_console_sink_id = EF_LOG_INVALID_SINK_ID;
static ef_time_ns        g_start_time = 0;
static bool              g_console_use_color = false;

static const char *const k_level_strings[EF_LOG_LEVEL_COUNT] = {
    [EF_LOG_LEVEL_TRACE] = "TRACE", [EF_LOG_LEVEL_DEBUG] = "DEBUG", [EF_LOG_LEVEL_INFO] = "INFO",
    [EF_LOG_LEVEL_WARN] = "WARN",   [EF_LOG_LEVEL_ERROR] = "ERROR", [EF_LOG_LEVEL_FATAL] = "FATAL",
};

static const char *const k_level_colors[EF_LOG_LEVEL_COUNT] = {
    [EF_LOG_LEVEL_TRACE] = "\x1b[90m", [EF_LOG_LEVEL_DEBUG] = "\x1b[36m",
    [EF_LOG_LEVEL_INFO] = "\x1b[37m",  [EF_LOG_LEVEL_WARN] = "\x1b[33m",
    [EF_LOG_LEVEL_ERROR] = "\x1b[31m", [EF_LOG_LEVEL_FATAL] = "\x1b[35m",
};
#define EF_LOG_COLOR_RESET "\x1b[0m"

const char *ef_log_level_to_string(ef_log_level level) {
    if ((unsigned)level >= (unsigned)EF_LOG_LEVEL_COUNT) {
        return "UNKNOWN";
    }
    return k_level_strings[level];
}

static void ef_log_console_sink(void *userdata, ef_log_level level, const char *category,
                                 const char *message) {
    (void)userdata;
    FILE *stream = (level >= EF_LOG_LEVEL_WARN) ? stderr : stdout;
    const double uptime = ef_timer_seconds_since(g_start_time);
    if (g_console_use_color) {
        fprintf(stream, "%s[%10.4f] [%-5s] [%s]%s %s\n", k_level_colors[level], uptime,
                k_level_strings[level], category, EF_LOG_COLOR_RESET, message);
    } else {
        fprintf(stream, "[%10.4f] [%-5s] [%s] %s\n", uptime, k_level_strings[level], category,
                message);
    }
}

ef_result ef_log_init(ef_log_level min_level) {
    if (g_initialized) {
        return EF_ERROR_ALREADY_INITIALIZED;
    }
    if ((unsigned)min_level >= (unsigned)EF_LOG_LEVEL_COUNT) {
        return EF_ERROR_INVALID_ARGUMENT;
    }

    g_log_mutex = SDL_CreateMutex();
    if (g_log_mutex == nullptr) {
        return EF_ERROR_PLATFORM;
    }

    memset(g_sinks, 0, sizeof(g_sinks));
    g_next_sink_id = 1;
    g_min_level = min_level;
    g_start_time = ef_timer_now();

#if EF_PLATFORM_WINDOWS
    {
        HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD  mode = 0;
        if (handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode)) {
            SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        }
    }
#endif
    g_console_use_color = ef_isatty(EF_STDOUT_FILENO) != 0;
    g_initialized = true;

    const ef_result result = ef_log_add_sink(ef_log_console_sink, nullptr, &g_console_sink_id);
    if (result != EF_OK) {
        g_initialized = false;
        SDL_DestroyMutex(g_log_mutex);
        g_log_mutex = nullptr;
        return result;
    }

    return EF_OK;
}

void ef_log_shutdown(void) {
    if (!g_initialized) {
        return;
    }
    g_initialized = false;
    memset(g_sinks, 0, sizeof(g_sinks));
    g_console_sink_id = EF_LOG_INVALID_SINK_ID;
    SDL_DestroyMutex(g_log_mutex);
    g_log_mutex = nullptr;
}

void ef_log_set_min_level(ef_log_level level) {
    if (!g_initialized || (unsigned)level >= (unsigned)EF_LOG_LEVEL_COUNT) {
        return;
    }
    SDL_LockMutex(g_log_mutex);
    g_min_level = level;
    SDL_UnlockMutex(g_log_mutex);
}

ef_log_level ef_log_get_min_level(void) {
    if (!g_initialized) {
        return g_min_level;
    }
    SDL_LockMutex(g_log_mutex);
    const ef_log_level level = g_min_level;
    SDL_UnlockMutex(g_log_mutex);
    return level;
}

ef_result ef_log_add_sink(ef_log_sink_fn sink, void *userdata, ef_log_sink_id *out_id) {
    if (sink == nullptr || !g_initialized) {
        return EF_ERROR_INVALID_ARGUMENT;
    }

    SDL_LockMutex(g_log_mutex);
    ef_result result = EF_ERROR_CAPACITY_EXCEEDED;
    for (ef_u32 i = 0; i < EF_LOG_MAX_SINKS; ++i) {
        if (!g_sinks[i].active) {
            g_sinks[i].id = g_next_sink_id++;
            g_sinks[i].fn = sink;
            g_sinks[i].userdata = userdata;
            g_sinks[i].active = true;
            if (out_id != nullptr) {
                *out_id = g_sinks[i].id;
            }
            result = EF_OK;
            break;
        }
    }
    SDL_UnlockMutex(g_log_mutex);
    return result;
}

ef_result ef_log_remove_sink(ef_log_sink_id id) {
    if (id == EF_LOG_INVALID_SINK_ID || !g_initialized) {
        return EF_ERROR_INVALID_ARGUMENT;
    }

    SDL_LockMutex(g_log_mutex);
    ef_result result = EF_ERROR_NOT_FOUND;
    for (ef_u32 i = 0; i < EF_LOG_MAX_SINKS; ++i) {
        if (g_sinks[i].active && g_sinks[i].id == id) {
            g_sinks[i] = (ef_log_sink_entry){0};
            result = EF_OK;
            break;
        }
    }
    SDL_UnlockMutex(g_log_mutex);
    return result;
}

void ef_log_message(ef_log_level level, const char *category, const char *fmt, ...) {
    if (!g_initialized || (unsigned)level >= (unsigned)EF_LOG_LEVEL_COUNT || level < g_min_level) {
        return;
    }
    if (category == nullptr) {
        category = "general";
    }

    char buffer[EF_LOG_MESSAGE_CAPACITY];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    /* Snapshot the sink table under lock, then invoke sinks unlocked so a
     * sink calling back into ef_log_message (or add/remove_sink) cannot
     * deadlock on g_log_mutex. */
    ef_log_sink_entry snapshot[EF_LOG_MAX_SINKS];
    SDL_LockMutex(g_log_mutex);
    memcpy(snapshot, g_sinks, sizeof(snapshot));
    SDL_UnlockMutex(g_log_mutex);

    for (ef_u32 i = 0; i < EF_LOG_MAX_SINKS; ++i) {
        if (snapshot[i].active) {
            snapshot[i].fn(snapshot[i].userdata, level, category, buffer);
        }
    }
}
