#include "eisenfront/platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

TimeNs time_now_ns(void) {
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);

    /* Split whole/remainder seconds before multiplying by 1e9 so the
     * intermediate product cannot overflow a 64-bit value; see the
     * identical technique (and rationale) in the POSIX backend. */
    const uint64_t c = (uint64_t)counter.QuadPart;
    const uint64_t f = (uint64_t)frequency.QuadPart;
    const uint64_t whole_seconds = c / f;
    const uint64_t remainder = c % f;
    return (whole_seconds * 1000000000ull) + (remainder * 1000000000ull) / f;
}

TimeNs time_frequency(void) {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return (uint64_t)frequency.QuadPart;
}

double time_ns_to_seconds(TimeNs duration) {
    return (double)duration / 1e9;
}

TimeNs time_seconds_to_ns(double seconds) {
    return (TimeNs)(seconds * 1e9);
}
