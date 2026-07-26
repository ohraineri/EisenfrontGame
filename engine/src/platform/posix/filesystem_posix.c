/* _GNU_SOURCE exposes POSIX.1-2008 + glibc extensions (clock_gettime,
 * nanosleep, realpath, readlink, pthread_setname_np, syscall/SYS_gettid, ...)
 * that -std=c23 hides by default since CMAKE_C_EXTENSIONS is OFF. Harmless
 * on macOS/BSD libc, which do not gate declarations on it. */
#define _GNU_SOURCE

#include "eisenfront/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static FileKind kind_from_stat(const struct stat *st) {
    if (S_ISREG(st->st_mode)) {
        return FILE_KIND_REGULAR;
    }
    if (S_ISDIR(st->st_mode)) {
        return FILE_KIND_DIRECTORY;
    }
    return FILE_KIND_OTHER;
}

bool file_exists(const char *path) {
    if (path == nullptr) {
        return false;
    }
    struct stat st;
    return stat(path, &st) == 0;
}

Result file_stat(const char *path, FileInfo *out_info) {
    if (path == nullptr || out_info == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        return RESULT_ERROR_NOT_FOUND;
    }

    out_info->kind = kind_from_stat(&st);
    out_info->size_bytes = (uint64_t)st.st_size;
    out_info->modified_time_unix = (int64_t)st.st_mtime;
    return RESULT_OK;
}

Result file_delete(const char *path) {
    if (path == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (remove(path) != 0) {
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
