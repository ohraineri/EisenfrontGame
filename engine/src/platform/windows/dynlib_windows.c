#include "eisenfront/platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>

struct DynLib {
    HMODULE handle;
};

Result dynlib_open(const char *path, DynLib **out_lib) {
    if (path == nullptr || out_lib == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    HMODULE handle = LoadLibraryA(path);
    if (handle == nullptr) {
        return RESULT_ERROR_NOT_FOUND;
    }

    DynLib *lib = malloc(sizeof(*lib));
    if (lib == nullptr) {
        FreeLibrary(handle);
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
    FreeLibrary(lib->handle);
    free(lib);
}

Result dynlib_symbol(DynLib *lib, const char *name, void **out_symbol) {
    if (lib == nullptr || name == nullptr || out_symbol == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    FARPROC symbol = GetProcAddress(lib->handle, name);
    if (symbol == nullptr) {
        return RESULT_ERROR_NOT_FOUND;
    }

    /* FARPROC (function pointer) -> void* is conditionally supported by
     * strict ISO C, but every compiler this engine targets (GCC, Clang,
     * MSVC) guarantees it round-trips correctly, matching how dynlib_symbol
     * is used: callers cast the returned void* back to a function pointer
     * type themselves. */
    *out_symbol = (void *)symbol;
    return RESULT_OK;
}
