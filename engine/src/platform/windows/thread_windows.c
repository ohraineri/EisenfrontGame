#include "eisenfront/platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>

/*
 * Mirrors the POSIX backend's ownership handoff: a ThreadContext is freed
 * by whichever of {trampoline finishing, thread_detach being called}
 * happens last, guarded by a critical section. Unlike pthreads, closing a
 * Windows thread HANDLE does not require the thread to have finished, so
 * detach here is just "the caller no longer wants to join or query this
 * thread" bookkeeping plus CloseHandle.
 */
typedef struct ThreadContext {
    ThreadFn          fn;
    void             *userdata;
    int               exit_code;
    CRITICAL_SECTION  state_lock;
    bool              finished;
    bool              detached;
} ThreadContext;

struct Thread {
    HANDLE          handle;
    ThreadContext  *context;
};

static DWORD WINAPI trampoline(LPVOID arg) {
    ThreadContext *ctx = arg;
    ctx->exit_code = ctx->fn(ctx->userdata);

    EnterCriticalSection(&ctx->state_lock);
    ctx->finished = true;
    const bool should_free = ctx->detached;
    LeaveCriticalSection(&ctx->state_lock);

    if (should_free) {
        DeleteCriticalSection(&ctx->state_lock);
        free(ctx);
    }
    return 0;
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
    InitializeCriticalSection(&ctx->state_lock);

    Thread *thread = malloc(sizeof(*thread));
    if (thread == nullptr) {
        DeleteCriticalSection(&ctx->state_lock);
        free(ctx);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    thread->context = ctx;

    thread->handle = CreateThread(nullptr, 0, trampoline, ctx, 0, nullptr);
    if (thread->handle == nullptr) {
        DeleteCriticalSection(&ctx->state_lock);
        free(ctx);
        free(thread);
        return RESULT_ERROR_PLATFORM;
    }

    if (desc->name != nullptr) {
        wchar_t wide_name[64];
        const int written =
            MultiByteToWideChar(CP_UTF8, 0, desc->name, -1, wide_name, (int)(sizeof(wide_name) / sizeof(wide_name[0])));
        if (written > 0) {
            SetThreadDescription(thread->handle, wide_name);
        }
    }

    *out_thread = thread;
    return RESULT_OK;
}

Result thread_join(Thread *thread, int *out_exit_code) {
    if (thread == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (WaitForSingleObject(thread->handle, INFINITE) != WAIT_OBJECT_0) {
        return RESULT_ERROR_PLATFORM;
    }
    if (out_exit_code != nullptr) {
        *out_exit_code = thread->context->exit_code;
    }
    CloseHandle(thread->handle);
    DeleteCriticalSection(&thread->context->state_lock);
    free(thread->context);
    free(thread);
    return RESULT_OK;
}

void thread_detach(Thread *thread) {
    if (thread == nullptr) {
        return;
    }

    ThreadContext *ctx = thread->context;
    EnterCriticalSection(&ctx->state_lock);
    ctx->detached = true;
    const bool should_free = ctx->finished;
    LeaveCriticalSection(&ctx->state_lock);

    CloseHandle(thread->handle);
    if (should_free) {
        DeleteCriticalSection(&ctx->state_lock);
        free(ctx);
    }
    free(thread);
}

uint64_t thread_current_id(void) {
    return (uint64_t)GetCurrentThreadId();
}
