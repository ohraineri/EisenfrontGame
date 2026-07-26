#include "eisenfront/foundation/ef_module.h"

#include "eisenfront/foundation/ef_assert.h"
#include "eisenfront/foundation/ef_log.h"

#include <stdlib.h>
#include <string.h>

#define EF_MODULE_LOG_CATEGORY "module"

typedef struct ef_module_entry {
    ef_module_desc desc;
    bool           initialized;
} ef_module_entry;

struct ef_module_registry {
    ef_module_entry *entries;
    ef_u32           capacity;
    ef_u32           count;
};

ef_result ef_module_registry_create(ef_u32 capacity, ef_module_registry **out_registry) {
    EF_ASSERT(out_registry != nullptr);
    if (out_registry == nullptr) {
        return EF_ERROR_INVALID_ARGUMENT;
    }
    if (capacity == 0) {
        return EF_ERROR_INVALID_ARGUMENT;
    }

    ef_module_registry *registry = malloc(sizeof(*registry));
    if (registry == nullptr) {
        return EF_ERROR_OUT_OF_MEMORY;
    }

    registry->entries = calloc(capacity, sizeof(ef_module_entry));
    if (registry->entries == nullptr) {
        free(registry);
        return EF_ERROR_OUT_OF_MEMORY;
    }

    registry->capacity = capacity;
    registry->count = 0;
    *out_registry = registry;
    return EF_OK;
}

void ef_module_registry_destroy(ef_module_registry *registry) {
    if (registry == nullptr) {
        return;
    }
    free(registry->entries);
    free(registry);
}

ef_result ef_module_registry_register(ef_module_registry *registry, const ef_module_desc *desc) {
    EF_ASSERT(registry != nullptr);
    EF_ASSERT(desc != nullptr);
    if (registry == nullptr || desc == nullptr) {
        return EF_ERROR_INVALID_ARGUMENT;
    }
    if (desc->name == nullptr || desc->name[0] == '\0') {
        return EF_ERROR_INVALID_ARGUMENT;
    }
    if (registry->count >= registry->capacity) {
        return EF_ERROR_CAPACITY_EXCEEDED;
    }
    for (ef_u32 i = 0; i < registry->count; ++i) {
        if (strcmp(registry->entries[i].desc.name, desc->name) == 0) {
            return EF_ERROR_ALREADY_EXISTS;
        }
    }

    registry->entries[registry->count].desc = *desc;
    registry->entries[registry->count].initialized = false;
    registry->count += 1;
    return EF_OK;
}

ef_result ef_module_registry_init_all(ef_module_registry *registry) {
    EF_ASSERT(registry != nullptr);
    if (registry == nullptr) {
        return EF_ERROR_INVALID_ARGUMENT;
    }

    for (ef_u32 i = 0; i < registry->count; ++i) {
        ef_module_entry *entry = &registry->entries[i];
        EF_LOG_DEBUG(EF_MODULE_LOG_CATEGORY, "Initializing module '%s'", entry->desc.name);

        ef_result result = EF_OK;
        if (entry->desc.on_init != nullptr) {
            result = entry->desc.on_init(entry->desc.userdata);
        }

        if (result != EF_OK) {
            EF_LOG_ERROR(EF_MODULE_LOG_CATEGORY, "Module '%s' failed to initialize: %s",
                         entry->desc.name, ef_result_to_string(result));

            for (ef_u32 j = i; j-- > 0;) {
                ef_module_entry *rollback = &registry->entries[j];
                if (rollback->initialized && rollback->desc.on_shutdown != nullptr) {
                    rollback->desc.on_shutdown(rollback->desc.userdata);
                }
                rollback->initialized = false;
            }
            return result;
        }

        entry->initialized = true;
    }

    return EF_OK;
}

void ef_module_registry_shutdown_all(ef_module_registry *registry) {
    EF_ASSERT(registry != nullptr);
    if (registry == nullptr) {
        return;
    }

    for (ef_u32 i = registry->count; i-- > 0;) {
        ef_module_entry *entry = &registry->entries[i];
        if (entry->initialized) {
            EF_LOG_DEBUG(EF_MODULE_LOG_CATEGORY, "Shutting down module '%s'", entry->desc.name);
            if (entry->desc.on_shutdown != nullptr) {
                entry->desc.on_shutdown(entry->desc.userdata);
            }
            entry->initialized = false;
        }
    }
}

ef_u32 ef_module_registry_count(const ef_module_registry *registry) {
    EF_ASSERT(registry != nullptr);
    return registry != nullptr ? registry->count : 0;
}

const char *ef_module_registry_name_at(const ef_module_registry *registry, ef_u32 index) {
    EF_ASSERT(registry != nullptr);
    if (registry == nullptr || index >= registry->count) {
        return nullptr;
    }
    return registry->entries[index].desc.name;
}
