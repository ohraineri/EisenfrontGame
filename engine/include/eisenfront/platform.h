/*
 * Platform - operating system abstraction.
 *
 * Responsibility: hide Linux/Windows/macOS differences behind one API so
 * the rest of the engine (Core and everything built on it) never calls a
 * native OS function or includes an OS header directly. No type or
 * function declared here leaks a platform-specific representation
 * (HANDLE, pthread_t, DIR*, ...); every stateful object is an opaque
 * pointer allocated and destroyed through this API.
 *
 * Ownership: the caller owns every handle returned by a `_create` /
 * `_open` function and must release it with the matching `_destroy` /
 * `_close` function exactly once.
 *
 * Dependencies: none within the engine. Platform is the lowest layer;
 * everything else (Core, and transitively every other module) depends on
 * it, never the reverse. This header therefore also owns the compiler/OS
 * detection macros and the engine-wide Result type, since both are needed
 * before any higher module exists.
 *
 * Lifetime: the free functions in this header (time_*, sleep_*, path_*,
 * file_*, directory_* except iterators) require no setup and may be
 * called at any point in the process lifetime. Mutex, Thread, DynLib and
 * DirectoryIterator are individually owned objects with their own
 * create/destroy pairs.
 */
#ifndef EISENFRONT_PLATFORM_H
#define EISENFRONT_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

/* ==================================================================== *
 * Platform / compiler detection
 * ==================================================================== */

#if defined(_WIN32) || defined(_WIN64)
#    define PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#    define PLATFORM_MACOS 1
#elif defined(__linux__)
#    define PLATFORM_LINUX 1
#else
#    error "Eisenfront: unsupported platform"
#endif

#if !defined(PLATFORM_WINDOWS)
#    define PLATFORM_WINDOWS 0
#endif
#if !defined(PLATFORM_MACOS)
#    define PLATFORM_MACOS 0
#endif
#if !defined(PLATFORM_LINUX)
#    define PLATFORM_LINUX 0
#endif

#if defined(__clang__)
#    define COMPILER_CLANG 1
#elif defined(__GNUC__)
#    define COMPILER_GCC 1
#elif defined(_MSC_VER)
#    define COMPILER_MSVC 1
#else
#    error "Eisenfront: unsupported compiler"
#endif

#if !defined(COMPILER_CLANG)
#    define COMPILER_CLANG 0
#endif
#if !defined(COMPILER_GCC)
#    define COMPILER_GCC 0
#endif
#if !defined(COMPILER_MSVC)
#    define COMPILER_MSVC 0
#endif

#if defined(NDEBUG)
#    define BUILD_DEBUG 0
#else
#    define BUILD_DEBUG 1
#endif

#if defined(EISENFRONT_SHARED)
#    if PLATFORM_WINDOWS
#        if defined(EISENFRONT_BUILDING)
#            define ENGINE_API __declspec(dllexport)
#        else
#            define ENGINE_API __declspec(dllimport)
#        endif
#    elif COMPILER_GCC || COMPILER_CLANG
#        define ENGINE_API __attribute__((visibility("default")))
#    else
#        define ENGINE_API
#    endif
#else
#    define ENGINE_API
#endif

#if COMPILER_GCC || COMPILER_CLANG
#    define UNUSED __attribute__((unused))
#    define PRINTF_FORMAT(fmt_index, arg_index) __attribute__((format(printf, fmt_index, arg_index)))
#else
#    define UNUSED
#    define PRINTF_FORMAT(fmt_index, arg_index)
#endif

/* ==================================================================== *
 * Result - engine-wide error code, shared by every module
 * ==================================================================== */

typedef enum Result {
    RESULT_OK = 0,
    RESULT_ERROR_UNKNOWN,
    RESULT_ERROR_INVALID_ARGUMENT,
    RESULT_ERROR_OUT_OF_MEMORY,
    RESULT_ERROR_NOT_INITIALIZED,
    RESULT_ERROR_ALREADY_INITIALIZED,
    RESULT_ERROR_NOT_FOUND,
    RESULT_ERROR_ALREADY_EXISTS,
    RESULT_ERROR_CAPACITY_EXCEEDED,
    RESULT_ERROR_INVALID_STATE,
    RESULT_ERROR_PERMISSION_DENIED,
    RESULT_ERROR_IO,
    RESULT_ERROR_TIMEOUT,
    RESULT_ERROR_NOT_SUPPORTED,
    RESULT_ERROR_PLATFORM,
    RESULT_ERROR_MODULE_INIT_FAILED,
    RESULT_COUNT
} Result;

ENGINE_API const char *result_to_string(Result result);

/* ==================================================================== *
 * Time - monotonic high resolution clock
 * ==================================================================== */

typedef uint64_t TimeNs;

ENGINE_API TimeNs time_now_ns(void);
ENGINE_API TimeNs time_frequency(void);
ENGINE_API double time_ns_to_seconds(TimeNs duration);
ENGINE_API TimeNs time_seconds_to_ns(double seconds);

/* ==================================================================== *
 * Sleep
 * ==================================================================== */

ENGINE_API void sleep_ms(uint32_t milliseconds);
ENGINE_API void sleep_ns(uint64_t nanoseconds);
ENGINE_API void sleep_yield(void);

/* ==================================================================== *
 * Mutex
 * ==================================================================== */

typedef struct Mutex Mutex;

ENGINE_API Result mutex_create(Mutex **out_mutex);
ENGINE_API void   mutex_destroy(Mutex *mutex);
ENGINE_API void   mutex_lock(Mutex *mutex);
ENGINE_API bool   mutex_try_lock(Mutex *mutex);
ENGINE_API void   mutex_unlock(Mutex *mutex);

/* ==================================================================== *
 * Thread
 * ==================================================================== */

typedef struct Thread Thread;

typedef int (*ThreadFn)(void *userdata);

typedef struct ThreadDesc {
    const char *name;
    ThreadFn    fn;
    void       *userdata;
} ThreadDesc;

ENGINE_API Result   thread_create(const ThreadDesc *desc, Thread **out_thread);
ENGINE_API Result   thread_join(Thread *thread, int *out_exit_code);
ENGINE_API void     thread_detach(Thread *thread);
ENGINE_API uint64_t thread_current_id(void);

/* ==================================================================== *
 * Dynamic libraries
 * ==================================================================== */

typedef struct DynLib DynLib;

ENGINE_API Result dynlib_open(const char *path, DynLib **out_lib);
ENGINE_API void   dynlib_close(DynLib *lib);
ENGINE_API Result dynlib_symbol(DynLib *lib, const char *name, void **out_symbol);

/* ==================================================================== *
 * Filesystem
 * ==================================================================== */

typedef enum FileKind {
    FILE_KIND_NONE = 0,
    FILE_KIND_REGULAR,
    FILE_KIND_DIRECTORY,
    FILE_KIND_OTHER,
} FileKind;

typedef struct FileInfo {
    FileKind kind;
    uint64_t size_bytes;
    int64_t  modified_time_unix;
} FileInfo;

ENGINE_API bool   file_exists(const char *path);
ENGINE_API Result file_stat(const char *path, FileInfo *out_info);
ENGINE_API Result file_delete(const char *path);
ENGINE_API Result file_read_all(const char *path, void **out_data, size_t *out_size);
ENGINE_API void   file_free_buffer(void *data);
ENGINE_API Result file_write_all(const char *path, const void *data, size_t size);

/* ==================================================================== *
 * Paths
 * ==================================================================== */

#if PLATFORM_WINDOWS
#    define PATH_SEPARATOR '\\'
#else
#    define PATH_SEPARATOR '/'
#endif

ENGINE_API Result path_join(char *out_buffer, size_t buffer_size, const char *lhs, const char *rhs);
ENGINE_API Result path_dirname(const char *path, char *out_buffer, size_t buffer_size);
ENGINE_API Result path_basename(const char *path, char *out_buffer, size_t buffer_size);
ENGINE_API Result path_extension(const char *path, char *out_buffer, size_t buffer_size);
ENGINE_API Result path_absolute(const char *path, char *out_buffer, size_t buffer_size);
ENGINE_API Result path_executable_dir(char *out_buffer, size_t buffer_size);

/* ==================================================================== *
 * Directories
 * ==================================================================== */

typedef struct DirectoryEntry {
    char     name[256];
    FileKind kind;
} DirectoryEntry;

ENGINE_API Result directory_create(const char *path);
ENGINE_API Result directory_remove(const char *path);
ENGINE_API Result directory_current(char *out_buffer, size_t buffer_size);

typedef struct DirectoryIterator DirectoryIterator;

ENGINE_API Result directory_iterator_open(const char *path, DirectoryIterator **out_iterator);
ENGINE_API bool   directory_iterator_next(DirectoryIterator *iterator, DirectoryEntry *out_entry);
ENGINE_API void   directory_iterator_close(DirectoryIterator *iterator);

#endif /* EISENFRONT_PLATFORM_H */
