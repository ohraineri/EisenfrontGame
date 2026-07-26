/*
 * Eisenfront Foundation - module registration.
 *
 * A module is a name plus two optional callbacks; the registry only
 * sequences them, it has no opinion on what a module actually does. The
 * engine calls ef_module_registry_init_all() once, in registration order;
 * if any on_init fails, everything already started is unwound in reverse
 * order (LIFO) before the error is returned, so a failed engine start never
 * leaves a partially-initialized module set behind. Shutdown always runs
 * LIFO so a module never outlives one it depends on.
 */
#ifndef EISENFRONT_FOUNDATION_EF_MODULE_H
#define EISENFRONT_FOUNDATION_EF_MODULE_H

#include "ef_error.h"
#include "ef_types.h"

typedef ef_result (*ef_module_init_fn)(void *userdata);
typedef void (*ef_module_shutdown_fn)(void *userdata);

typedef struct ef_module_desc {
    const char           *name;
    void                 *userdata;
    ef_module_init_fn     on_init;     /* may be nullptr */
    ef_module_shutdown_fn on_shutdown; /* may be nullptr */
} ef_module_desc;

typedef struct ef_module_registry ef_module_registry;

ef_result ef_module_registry_create(ef_u32 capacity, ef_module_registry **out_registry);
void      ef_module_registry_destroy(ef_module_registry *registry);

ef_result ef_module_registry_register(ef_module_registry *registry, const ef_module_desc *desc);

ef_result ef_module_registry_init_all(ef_module_registry *registry);
void      ef_module_registry_shutdown_all(ef_module_registry *registry);

ef_u32      ef_module_registry_count(const ef_module_registry *registry);
const char *ef_module_registry_name_at(const ef_module_registry *registry, ef_u32 index);

#endif /* EISENFRONT_FOUNDATION_EF_MODULE_H */
