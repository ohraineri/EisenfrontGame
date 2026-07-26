/*
 * Eisenfront Foundation - fixed-width type aliases and small utility macros
 * shared by every Foundation header.
 */
#ifndef EISENFRONT_FOUNDATION_EF_TYPES_H
#define EISENFRONT_FOUNDATION_EF_TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef int8_t  ef_i8;
typedef int16_t ef_i16;
typedef int32_t ef_i32;
typedef int64_t ef_i64;

typedef uint8_t  ef_u8;
typedef uint16_t ef_u16;
typedef uint32_t ef_u32;
typedef uint64_t ef_u64;

typedef float  ef_f32;
typedef double ef_f64;

typedef size_t    ef_usize;
typedef ptrdiff_t ef_isize;

static_assert(sizeof(ef_f32) == 4, "ef_f32 must be IEEE-754 binary32");
static_assert(sizeof(ef_f64) == 8, "ef_f64 must be IEEE-754 binary64");

#define EF_ARRAY_COUNT(array) (sizeof(array) / sizeof((array)[0]))
#define EF_BIT(n) (1u << (n))

#endif /* EISENFRONT_FOUNDATION_EF_TYPES_H */
