#include "eisenfront/platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string.h>

Result path_absolute(const char *path, char *out_buffer, size_t buffer_size) {
    if (path == nullptr || out_buffer == nullptr || buffer_size == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        return RESULT_ERROR_NOT_FOUND;
    }

    char full[MAX_PATH];
    const DWORD written = GetFullPathNameA(path, sizeof(full), full, nullptr);
    if (written == 0 || written >= sizeof(full)) {
        return RESULT_ERROR_IO;
    }

    const size_t len = strlen(full);
    if (len + 1 > buffer_size) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }
    memcpy(out_buffer, full, len + 1);
    return RESULT_OK;
}

Result path_executable_dir(char *out_buffer, size_t buffer_size) {
    if (out_buffer == nullptr || buffer_size == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    char exe_path[MAX_PATH];
    const DWORD written = GetModuleFileNameA(nullptr, exe_path, sizeof(exe_path));
    if (written == 0 || written >= sizeof(exe_path)) {
        return RESULT_ERROR_PLATFORM;
    }

    return path_dirname(exe_path, out_buffer, buffer_size);
}
