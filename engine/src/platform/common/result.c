#include "eisenfront/platform.h"

static const char *const k_result_strings[RESULT_COUNT] = {
    [RESULT_OK] = "OK",
    [RESULT_ERROR_UNKNOWN] = "Unknown error",
    [RESULT_ERROR_INVALID_ARGUMENT] = "Invalid argument",
    [RESULT_ERROR_OUT_OF_MEMORY] = "Out of memory",
    [RESULT_ERROR_NOT_INITIALIZED] = "Not initialized",
    [RESULT_ERROR_ALREADY_INITIALIZED] = "Already initialized",
    [RESULT_ERROR_NOT_FOUND] = "Not found",
    [RESULT_ERROR_ALREADY_EXISTS] = "Already exists",
    [RESULT_ERROR_CAPACITY_EXCEEDED] = "Capacity exceeded",
    [RESULT_ERROR_INVALID_STATE] = "Invalid state",
    [RESULT_ERROR_PERMISSION_DENIED] = "Permission denied",
    [RESULT_ERROR_IO] = "I/O error",
    [RESULT_ERROR_TIMEOUT] = "Timed out",
    [RESULT_ERROR_NOT_SUPPORTED] = "Not supported",
    [RESULT_ERROR_PLATFORM] = "Platform error",
    [RESULT_ERROR_MODULE_INIT_FAILED] = "Module initialization failed",
};

const char *result_to_string(Result result) {
    if ((unsigned)result >= (unsigned)RESULT_COUNT) {
        return "Invalid result code";
    }
    return k_result_strings[result];
}
