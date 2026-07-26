#include "eisenfront/platform.h"

#include <pthread.h>
#include <stdlib.h>

struct Mutex {
    pthread_mutex_t handle;
};

Result mutex_create(Mutex **out_mutex) {
    if (out_mutex == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    Mutex *mutex = malloc(sizeof(*mutex));
    if (mutex == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    if (pthread_mutex_init(&mutex->handle, nullptr) != 0) {
        free(mutex);
        return RESULT_ERROR_PLATFORM;
    }

    *out_mutex = mutex;
    return RESULT_OK;
}

void mutex_destroy(Mutex *mutex) {
    if (mutex == nullptr) {
        return;
    }
    pthread_mutex_destroy(&mutex->handle);
    free(mutex);
}

void mutex_lock(Mutex *mutex) {
    pthread_mutex_lock(&mutex->handle);
}

bool mutex_try_lock(Mutex *mutex) {
    return pthread_mutex_trylock(&mutex->handle) == 0;
}

void mutex_unlock(Mutex *mutex) {
    pthread_mutex_unlock(&mutex->handle);
}
