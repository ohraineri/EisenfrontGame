/* _GNU_SOURCE exposes POSIX.1-2008 + glibc extensions (clock_gettime,
 * nanosleep, realpath, readlink, pthread_setname_np, syscall/SYS_gettid, ...)
 * that -std=c23 hides by default since CMAKE_C_EXTENSIONS is OFF. Harmless
 * on macOS/BSD libc, which do not gate declarations on it. */
#define _GNU_SOURCE

#include "eisenfront/platform.h"

#include <errno.h>
#include <sched.h>
#include <time.h>

static void sleep_ns_impl(uint64_t nanoseconds) {
    struct timespec ts;
    ts.tv_sec = (time_t)(nanoseconds / 1000000000ull);
    ts.tv_nsec = (long)(nanoseconds % 1000000000ull);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
        /* interrupted by a signal; ts was updated with the remaining time */
    }
}

void sleep_ms(uint32_t milliseconds) {
    sleep_ns_impl((uint64_t)milliseconds * 1000000ull);
}

void sleep_ns(uint64_t nanoseconds) {
    sleep_ns_impl(nanoseconds);
}

void sleep_yield(void) {
    sched_yield();
}
