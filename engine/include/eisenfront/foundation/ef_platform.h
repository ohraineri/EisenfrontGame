/*
 * Eisenfront Foundation - platform and compiler detection.
 *
 * Every other Foundation header includes this file. It never depends on
 * anything else in the engine and never includes SDL3 or OS headers, so it
 * is safe to include from any translation unit without pulling in
 * platform-specific dependencies.
 */
#ifndef EISENFRONT_FOUNDATION_EF_PLATFORM_H
#define EISENFRONT_FOUNDATION_EF_PLATFORM_H

/* ---- Platform ----------------------------------------------------- */

#if defined(_WIN32) || defined(_WIN64)
#    define EF_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#    define EF_PLATFORM_MACOS 1
#elif defined(__linux__)
#    define EF_PLATFORM_LINUX 1
#else
#    error "Eisenfront: unsupported platform"
#endif

#if !defined(EF_PLATFORM_WINDOWS)
#    define EF_PLATFORM_WINDOWS 0
#endif
#if !defined(EF_PLATFORM_MACOS)
#    define EF_PLATFORM_MACOS 0
#endif
#if !defined(EF_PLATFORM_LINUX)
#    define EF_PLATFORM_LINUX 0
#endif

/* ---- Compiler ------------------------------------------------------ */

#if defined(__clang__)
#    define EF_COMPILER_CLANG 1
#elif defined(__GNUC__)
#    define EF_COMPILER_GCC 1
#elif defined(_MSC_VER)
#    define EF_COMPILER_MSVC 1
#else
#    error "Eisenfront: unsupported compiler"
#endif

#if !defined(EF_COMPILER_CLANG)
#    define EF_COMPILER_CLANG 0
#endif
#if !defined(EF_COMPILER_GCC)
#    define EF_COMPILER_GCC 0
#endif
#if !defined(EF_COMPILER_MSVC)
#    define EF_COMPILER_MSVC 0
#endif

/* ---- Build configuration ------------------------------------------- */

#if defined(NDEBUG)
#    define EF_BUILD_DEBUG 0
#else
#    define EF_BUILD_DEBUG 1
#endif

/* ---- Symbol visibility (for a future shared-library build) --------- */

#if defined(EISENFRONT_SHARED)
#    if EF_PLATFORM_WINDOWS
#        if defined(EISENFRONT_BUILDING)
#            define EF_API __declspec(dllexport)
#        else
#            define EF_API __declspec(dllimport)
#        endif
#    elif EF_COMPILER_GCC || EF_COMPILER_CLANG
#        define EF_API __attribute__((visibility("default")))
#    else
#        define EF_API
#    endif
#else
#    define EF_API
#endif

/* ---- Compiler attributes -------------------------------------------- */

#if EF_COMPILER_GCC || EF_COMPILER_CLANG
#    define EF_UNUSED __attribute__((unused))
#    define EF_PRINTF_FORMAT(fmt_index, arg_index) \
        __attribute__((format(printf, fmt_index, arg_index)))
#else
#    define EF_UNUSED
#    define EF_PRINTF_FORMAT(fmt_index, arg_index)
#endif

#endif /* EISENFRONT_FOUNDATION_EF_PLATFORM_H */
