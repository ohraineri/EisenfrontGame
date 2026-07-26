#include "eisenfront/platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>

struct Mutex {
    CRITICAL_SECTION handle;
};

Result mutex_create(Mutex **out_mutex) {
    if (out_mutex == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    Mutex *mutex = malloc(sizeof(*mutex));
    if (mutex == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    InitializeCriticalSection(&mutex->handle);
    *out_mutex = mutex;
    return RESULT_OK;
}

void mutex_destroy(Mutex *mutex) {
    if (mutex == nullptr) {
        return;
    }
    DeleteCriticalSection(&mutex->handle);
    free(mutex);
}

void mutex_lock(Mutex *mutex) {
    EnterCriticalSection(&mutex->handle);
}

bool mutex_try_lock(Mutex *mutex) {
    return TryEnterCriticalSection(&mutex->handle) != 0;
}

void mutex_unlock(Mutex *mutex) {
    LeaveCriticalSection(&mutex->handle);
}
