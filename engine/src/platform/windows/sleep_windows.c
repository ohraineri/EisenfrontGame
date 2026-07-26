#include "eisenfront/platform.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void sleep_ms(uint32_t milliseconds) {
    Sleep((DWORD)milliseconds);
}

void sleep_ns(uint64_t nanoseconds) {
    /* Sleep() only offers millisecond granularity; round up so a caller
     * asking for a nonzero duration never gets rounded down to zero. */
    const uint64_t ms = (nanoseconds + 999999ull) / 1000000ull;
    Sleep((DWORD)ms);
}

void sleep_yield(void) {
    SwitchToThread();
}
