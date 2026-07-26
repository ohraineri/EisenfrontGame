#include "eisenfront/platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>

static int64_t filetime_to_unix(FILETIME ft) {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    /* FILETIME is 100ns ticks since 1601-01-01; 116444736000000000 is the
     * offset in ticks to the Unix epoch (1970-01-01). */
    return (int64_t)((u.QuadPart - 116444736000000000ull) / 10000000ull);
}

bool file_exists(const char *path) {
    if (path == nullptr) {
        return false;
    }
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

Result file_stat(const char *path, FileInfo *out_info) {
    if (path == nullptr || out_info == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &data)) {
        return RESULT_ERROR_NOT_FOUND;
    }

    out_info->kind = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? FILE_KIND_DIRECTORY
                                                                         : FILE_KIND_REGULAR;
    out_info->size_bytes =
        ((uint64_t)data.nFileSizeHigh << 32) | (uint64_t)data.nFileSizeLow;
    out_info->modified_time_unix = filetime_to_unix(data.ftLastWriteTime);
    return RESULT_OK;
}

Result file_delete(const char *path) {
    if (path == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!DeleteFileA(path)) {
        return RESULT_ERROR_IO;
    }
    return RESULT_OK;
}

Result file_read_all(const char *path, void **out_data, size_t *out_size) {
    if (path == nullptr || out_data == nullptr || out_size == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    FILE *file = fopen(path, "rb");
    if (file == nullptr) {
        return RESULT_ERROR_NOT_FOUND;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return RESULT_ERROR_IO;
    }
    const long size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return RESULT_ERROR_IO;
    }

    void *buffer = malloc((size_t)size);
    if (buffer == nullptr && size > 0) {
        fclose(file);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    if (size > 0 && fread(buffer, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        free(buffer);
        return RESULT_ERROR_IO;
    }
    fclose(file);

    *out_data = buffer;
    *out_size = (size_t)size;
    return RESULT_OK;
}

void file_free_buffer(void *data) {
    free(data);
}

Result file_write_all(const char *path, const void *data, size_t size) {
    if (path == nullptr || (data == nullptr && size > 0)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    FILE *file = fopen(path, "wb");
    if (file == nullptr) {
        return RESULT_ERROR_IO;
    }

    if (size > 0 && fwrite(data, 1, size, file) != size) {
        fclose(file);
        return RESULT_ERROR_IO;
    }
    fclose(file);
    return RESULT_OK;
}
