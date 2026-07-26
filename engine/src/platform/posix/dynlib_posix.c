/* _GNU_SOURCE exposes POSIX.1-2008 + glibc extensions (clock_gettime,
 * nanosleep, realpath, readlink, pthread_setname_np, syscall/SYS_gettid, ...)
 * that -std=c23 hides by default since CMAKE_C_EXTENSIONS is OFF. Harmless
 * on macOS/BSD libc, which do not gate declarations on it. */
#define _GNU_SOURCE

#include "eisenfront/platform.h"

#include <dlfcn.h>
#include <stdlib.h>

struct DynLib {
    void *handle;
};

Result dynlib_open(const char *path, DynLib **out_lib) {
    if (path == nullptr || out_lib == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    dlerror(); /* clear any pending error */
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        return RESULT_ERROR_NOT_FOUND;
    }

    DynLib *lib = malloc(sizeof(*lib));
    if (lib == nullptr) {
        dlclose(handle);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    lib->handle = handle;

    *out_lib = lib;
    return RESULT_OK;
}

void dynlib_close(DynLib *lib) {
    if (lib == nullptr) {
        return;
    }
    dlclose(lib->handle);
    free(lib);
}

Result dynlib_symbol(DynLib *lib, const char *name, void **out_symbol) {
    if (lib == nullptr || name == nullptr || out_symbol == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    dlerror();
    void *symbol = dlsym(lib->handle, name);
    if (symbol == nullptr && dlerror() != nullptr) {
        return RESULT_ERROR_NOT_FOUND;
    }

    *out_symbol = symbol;
    return RESULT_OK;
}
