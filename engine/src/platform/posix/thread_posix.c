/* _GNU_SOURCE exposes POSIX.1-2008 + glibc extensions (clock_gettime,
 * nanosleep, realpath, readlink, pthread_setname_np, syscall/SYS_gettid, ...)
 * that -std=c23 hides by default since CMAKE_C_EXTENSIONS is OFF. Harmless
 * on macOS/BSD libc, which do not gate declarations on it. */
#define _GNU_SOURCE

#include "eisenfront/platform.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(__linux__)
#    include <sys/syscall.h>
#    include <unistd.h>
#elif defined(__APPLE__)
#    include <pthread.h>
#endif

/*
 * A ThreadContext outlives the Thread handle whenever the thread is
 * detached: the caller may free its Thread immediately on detach while
 * the OS thread keeps running and still needs somewhere to store its
 * result. state_mutex arbitrates a one-time handoff of who frees the
 * context: whichever of {trampoline finishing, thread_detach being
 * called} happens last is responsible for the free.
 */
typedef struct ThreadContext {
    ThreadFn        fn;
    void           *userdata;
    char            name[16];
    int             exit_code;
    pthread_mutex_t state_mutex;
    bool            finished;
    bool            detached;
} ThreadContext;

struct Thread {
    pthread_t      handle;
    ThreadContext *context;
};

static void *trampoline(void *arg) {
    ThreadContext *ctx = arg;

    if (ctx->name[0] != '\0') {
#if defined(__APPLE__)
        pthread_setname_np(ctx->name);
#else
        pthread_setname_np(pthread_self(), ctx->name);
#endif
    }

    ctx->exit_code = ctx->fn(ctx->userdata);

    pthread_mutex_lock(&ctx->state_mutex);
    ctx->finished = true;
    const bool should_free = ctx->detached;
    pthread_mutex_unlock(&ctx->state_mutex);

    if (should_free) {
        pthread_mutex_destroy(&ctx->state_mutex);
        free(ctx);
    }
    return nullptr;
}

Result thread_create(const ThreadDesc *desc, Thread **out_thread) {
    if (desc == nullptr || desc->fn == nullptr || out_thread == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    ThreadContext *ctx = malloc(sizeof(*ctx));
    if (ctx == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    ctx->fn = desc->fn;
    ctx->userdata = desc->userdata;
    ctx->exit_code = 0;
    ctx->finished = false;
    ctx->detached = false;
    ctx->name[0] = '\0';
    if (desc->name != nullptr) {
        snprintf(ctx->name, sizeof(ctx->name), "%s", desc->name);
    }
    if (pthread_mutex_init(&ctx->state_mutex, nullptr) != 0) {
        free(ctx);
        return RESULT_ERROR_PLATFORM;
    }

    Thread *thread = malloc(sizeof(*thread));
    if (thread == nullptr) {
        pthread_mutex_destroy(&ctx->state_mutex);
        free(ctx);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    thread->context = ctx;

    if (pthread_create(&thread->handle, nullptr, trampoline, ctx) != 0) {
        pthread_mutex_destroy(&ctx->state_mutex);
        free(ctx);
        free(thread);
        return RESULT_ERROR_PLATFORM;
    }

    *out_thread = thread;
    return RESULT_OK;
}

Result thread_join(Thread *thread, int *out_exit_code) {
    if (thread == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (pthread_join(thread->handle, nullptr) != 0) {
        return RESULT_ERROR_PLATFORM;
    }
    if (out_exit_code != nullptr) {
        *out_exit_code = thread->context->exit_code;
    }
    pthread_mutex_destroy(&thread->context->state_mutex);
    free(thread->context);
    free(thread);
    return RESULT_OK;
}

void thread_detach(Thread *thread) {
    if (thread == nullptr) {
        return;
    }
    pthread_detach(thread->handle);

    ThreadContext *ctx = thread->context;
    pthread_mutex_lock(&ctx->state_mutex);
    ctx->detached = true;
    const bool should_free = ctx->finished;
    pthread_mutex_unlock(&ctx->state_mutex);

    if (should_free) {
        pthread_mutex_destroy(&ctx->state_mutex);
        free(ctx);
    }
    free(thread);
}

uint64_t thread_current_id(void) {
#if defined(__linux__)
    return (uint64_t)syscall(SYS_gettid);
#elif defined(__APPLE__)
    uint64_t tid = 0;
    pthread_threadid_np(nullptr, &tid);
    return tid;
#else
    return (uint64_t)(uintptr_t)pthread_self();
#endif
}
