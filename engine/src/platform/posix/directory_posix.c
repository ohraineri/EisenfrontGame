#include "eisenfront/platform.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

Result directory_create(const char *path) {
    if (path == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (mkdir(path, 0755) != 0) {
        return RESULT_ERROR_IO;
    }
    return RESULT_OK;
}

Result directory_remove(const char *path) {
    if (path == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (rmdir(path) != 0) {
        return RESULT_ERROR_IO;
    }
    return RESULT_OK;
}

Result directory_current(char *out_buffer, size_t buffer_size) {
    if (out_buffer == nullptr || buffer_size == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (getcwd(out_buffer, buffer_size) == nullptr) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }
    return RESULT_OK;
}

struct DirectoryIterator {
    DIR  *handle;
    char  base_path[4096];
};

Result directory_iterator_open(const char *path, DirectoryIterator **out_iterator) {
    if (path == nullptr || out_iterator == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    DIR *handle = opendir(path);
    if (handle == nullptr) {
        return RESULT_ERROR_NOT_FOUND;
    }

    DirectoryIterator *iterator = malloc(sizeof(*iterator));
    if (iterator == nullptr) {
        closedir(handle);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    iterator->handle = handle;
    snprintf(iterator->base_path, sizeof(iterator->base_path), "%s", path);

    *out_iterator = iterator;
    return RESULT_OK;
}

bool directory_iterator_next(DirectoryIterator *iterator, DirectoryEntry *out_entry) {
    if (iterator == nullptr || out_entry == nullptr) {
        return false;
    }

    struct dirent *entry;
    while ((entry = readdir(iterator->handle)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        snprintf(out_entry->name, sizeof(out_entry->name), "%s", entry->d_name);

        char full_path[4096 + 256];
        snprintf(full_path, sizeof(full_path), "%s/%s", iterator->base_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                out_entry->kind = FILE_KIND_DIRECTORY;
            } else if (S_ISREG(st.st_mode)) {
                out_entry->kind = FILE_KIND_REGULAR;
            } else {
                out_entry->kind = FILE_KIND_OTHER;
            }
        } else {
            out_entry->kind = FILE_KIND_NONE;
        }
        return true;
    }
    return false;
}

void directory_iterator_close(DirectoryIterator *iterator) {
    if (iterator == nullptr) {
        return;
    }
    closedir(iterator->handle);
    free(iterator);
}
