/* _GNU_SOURCE exposes POSIX.1-2008 + glibc extensions (clock_gettime,
 * nanosleep, realpath, readlink, pthread_setname_np, syscall/SYS_gettid, ...)
 * that -std=c23 hides by default since CMAKE_C_EXTENSIONS is OFF. Harmless
 * on macOS/BSD libc, which do not gate declarations on it. */
#define _GNU_SOURCE

#include "eisenfront/platform.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__APPLE__)
#    include <mach-o/dyld.h>
#endif

Result path_absolute(const char *path, char *out_buffer, size_t buffer_size) {
    if (path == nullptr || out_buffer == nullptr || buffer_size == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    char resolved[PATH_MAX];
    if (realpath(path, resolved) == nullptr) {
        return RESULT_ERROR_NOT_FOUND;
    }

    const size_t len = strlen(resolved);
    if (len + 1 > buffer_size) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }
    memcpy(out_buffer, resolved, len + 1);
    return RESULT_OK;
}

Result path_executable_dir(char *out_buffer, size_t buffer_size) {
    if (out_buffer == nullptr || buffer_size == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    char exe_path[PATH_MAX];

#if defined(__APPLE__)
    uint32_t size = sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0) {
        return RESULT_ERROR_PLATFORM;
    }
#else
    const ssize_t written = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (written < 0) {
        return RESULT_ERROR_PLATFORM;
    }
    exe_path[written] = '\0';
#endif

    return path_dirname(exe_path, out_buffer, buffer_size);
}
