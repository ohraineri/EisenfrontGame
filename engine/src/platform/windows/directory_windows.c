#include "eisenfront/platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Result directory_create(const char *path) {
    if (path == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!CreateDirectoryA(path, nullptr)) {
        return RESULT_ERROR_IO;
    }
    return RESULT_OK;
}

Result directory_remove(const char *path) {
    if (path == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!RemoveDirectoryA(path)) {
        return RESULT_ERROR_IO;
    }
    return RESULT_OK;
}

Result directory_current(char *out_buffer, size_t buffer_size) {
    if (out_buffer == nullptr || buffer_size == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    const DWORD written = GetCurrentDirectoryA((DWORD)buffer_size, out_buffer);
    if (written == 0 || written >= buffer_size) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }
    return RESULT_OK;
}

struct DirectoryIterator {
    HANDLE          handle;
    WIN32_FIND_DATAA find_data;
    bool             has_pending;
};

Result directory_iterator_open(const char *path, DirectoryIterator **out_iterator) {
    if (path == nullptr || out_iterator == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*", path);

    DirectoryIterator *iterator = malloc(sizeof(*iterator));
    if (iterator == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    iterator->handle = FindFirstFileA(pattern, &iterator->find_data);
    if (iterator->handle == INVALID_HANDLE_VALUE) {
        free(iterator);
        return RESULT_ERROR_NOT_FOUND;
    }
    iterator->has_pending = true;

    *out_iterator = iterator;
    return RESULT_OK;
}

bool directory_iterator_next(DirectoryIterator *iterator, DirectoryEntry *out_entry) {
    if (iterator == nullptr || out_entry == nullptr) {
        return false;
    }

    while (iterator->has_pending) {
        const WIN32_FIND_DATAA *data = &iterator->find_data;
        const bool is_dot = strcmp(data->cFileName, ".") == 0 || strcmp(data->cFileName, "..") == 0;

        if (!is_dot) {
            snprintf(out_entry->name, sizeof(out_entry->name), "%s", data->cFileName);
            out_entry->kind = (data->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                                   ? FILE_KIND_DIRECTORY
                                   : FILE_KIND_REGULAR;
            iterator->has_pending = FindNextFileA(iterator->handle, &iterator->find_data) != 0;
            return true;
        }

        iterator->has_pending = FindNextFileA(iterator->handle, &iterator->find_data) != 0;
    }
    return false;
}

void directory_iterator_close(DirectoryIterator *iterator) {
    if (iterator == nullptr) {
        return;
    }
    FindClose(iterator->handle);
    free(iterator);
}
