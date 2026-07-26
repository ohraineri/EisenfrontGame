/* _GNU_SOURCE exposes POSIX.1-2008 + glibc extensions (clock_gettime,
 * nanosleep, realpath, readlink, pthread_setname_np, syscall/SYS_gettid, ...)
 * that -std=c23 hides by default since CMAKE_C_EXTENSIONS is OFF. Harmless
 * on macOS/BSD libc, which do not gate declarations on it. */
#define _GNU_SOURCE

#include "eisenfront/platform.h"

#include <time.h>

TimeNs time_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((TimeNs)ts.tv_sec * 1000000000ull) + (TimeNs)ts.tv_nsec;
}

TimeNs time_frequency(void) {
    /* clock_gettime already reports nanoseconds directly; this exists for
     * API symmetry with a raw-counter backend. */
    return 1000000000ull;
}

double time_ns_to_seconds(TimeNs duration) {
    return (double)duration / 1e9;
}

TimeNs time_seconds_to_ns(double seconds) {
    return (TimeNs)(seconds * 1e9);
}
