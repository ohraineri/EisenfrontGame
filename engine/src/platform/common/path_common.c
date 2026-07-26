/*
 * Pure string manipulation shared by every platform backend. Recognizes
 * both '/' and '\\' as separators on input (asset paths are authored with
 * '/' but the engine must also cope with a Windows path handed back by
 * the OS), and writes PATH_SEPARATOR on output.
 */
#include "eisenfront/platform.h"

#include <stdint.h>
#include <string.h>

static bool is_separator(char c) {
    return c == '/' || c == '\\';
}

Result path_join(char *out_buffer, size_t buffer_size, const char *lhs, const char *rhs) {
    if (out_buffer == nullptr || buffer_size == 0 || lhs == nullptr || rhs == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    size_t lhs_len = strlen(lhs);
    while (lhs_len > 0 && is_separator(lhs[lhs_len - 1])) {
        lhs_len -= 1;
    }
    size_t rhs_start = 0;
    while (rhs[rhs_start] != '\0' && is_separator(rhs[rhs_start])) {
        rhs_start += 1;
    }
    const size_t rhs_len = strlen(rhs + rhs_start);

    const size_t needed = lhs_len + 1 + rhs_len + 1;
    if (needed > buffer_size) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }

    memcpy(out_buffer, lhs, lhs_len);
    out_buffer[lhs_len] = PATH_SEPARATOR;
    memcpy(out_buffer + lhs_len + 1, rhs + rhs_start, rhs_len);
    out_buffer[lhs_len + 1 + rhs_len] = '\0';
    return RESULT_OK;
}

Result path_dirname(const char *path, char *out_buffer, size_t buffer_size) {
    if (path == nullptr || out_buffer == nullptr || buffer_size == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const size_t len = strlen(path);
    size_t       last_sep = SIZE_MAX;
    for (size_t i = 0; i < len; ++i) {
        if (is_separator(path[i])) {
            last_sep = i;
        }
    }

    const char *result = ".";
    size_t      result_len = 1;
    if (last_sep != SIZE_MAX) {
        result = path;
        result_len = (last_sep == 0) ? 1 : last_sep;
    }

    if (result_len + 1 > buffer_size) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }
    memcpy(out_buffer, result, result_len);
    out_buffer[result_len] = '\0';
    return RESULT_OK;
}

Result path_basename(const char *path, char *out_buffer, size_t buffer_size) {
    if (path == nullptr || out_buffer == nullptr || buffer_size == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const size_t len = strlen(path);
    size_t       start = 0;
    for (size_t i = 0; i < len; ++i) {
        if (is_separator(path[i])) {
            start = i + 1;
        }
    }

    const size_t result_len = len - start;
    if (result_len + 1 > buffer_size) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }
    memcpy(out_buffer, path + start, result_len);
    out_buffer[result_len] = '\0';
    return RESULT_OK;
}

Result path_extension(const char *path, char *out_buffer, size_t buffer_size) {
    if (path == nullptr || out_buffer == nullptr || buffer_size == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    char   base[256];
    Result result = path_basename(path, base, sizeof(base));
    if (result != RESULT_OK) {
        return result;
    }

    const size_t base_len = strlen(base);
    size_t       dot = SIZE_MAX;
    for (size_t i = base_len; i-- > 1;) {
        if (base[i] == '.') {
            dot = i;
            break;
        }
    }

    const char  *ext = (dot == SIZE_MAX) ? "" : (base + dot + 1);
    const size_t ext_len = strlen(ext);
    if (ext_len + 1 > buffer_size) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }
    memcpy(out_buffer, ext, ext_len);
    out_buffer[ext_len] = '\0';
    return RESULT_OK;
}
