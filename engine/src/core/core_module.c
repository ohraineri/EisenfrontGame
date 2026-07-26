#include "eisenfront/core.h"

#include <stdlib.h>
#include <string.h>

#define MODULE_LOG_CATEGORY "module"

typedef struct ModuleEntry {
    ModuleDesc desc;
    bool       initialized;
} ModuleEntry;

struct ModuleRegistry {
    ModuleEntry *entries;
    uint32_t     capacity;
    uint32_t     count;
};

Result module_registry_create(uint32_t capacity, ModuleRegistry **out_registry) {
    ASSERT(out_registry != nullptr);
    if (out_registry == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (capacity == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    ModuleRegistry *registry = malloc(sizeof(*registry));
    if (registry == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    registry->entries = calloc(capacity, sizeof(ModuleEntry));
    if (registry->entries == nullptr) {
        free(registry);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    registry->capacity = capacity;
    registry->count = 0;
    *out_registry = registry;
    return RESULT_OK;
}

void module_registry_destroy(ModuleRegistry *registry) {
    if (registry == nullptr) {
        return;
    }
    free(registry->entries);
    free(registry);
}

Result module_registry_register(ModuleRegistry *registry, const ModuleDesc *desc) {
    ASSERT(registry != nullptr);
    ASSERT(desc != nullptr);
    if (registry == nullptr || desc == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (desc->name == nullptr || desc->name[0] == '\0') {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (registry->count >= registry->capacity) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }
    for (uint32_t i = 0; i < registry->count; ++i) {
        if (strcmp(registry->entries[i].desc.name, desc->name) == 0) {
            return RESULT_ERROR_ALREADY_EXISTS;
        }
    }

    registry->entries[registry->count].desc = *desc;
    registry->entries[registry->count].initialized = false;
    registry->count += 1;
    return RESULT_OK;
}

Result module_registry_init_all(ModuleRegistry *registry) {
    ASSERT(registry != nullptr);
    if (registry == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    for (uint32_t i = 0; i < registry->count; ++i) {
        ModuleEntry *entry = &registry->entries[i];
        LOG_DEBUG(MODULE_LOG_CATEGORY, "Initializing module '%s'", entry->desc.name);

        Result result = RESULT_OK;
        if (entry->desc.on_init != nullptr) {
            result = entry->desc.on_init(entry->desc.userdata);
        }

        if (result != RESULT_OK) {
            LOG_ERROR(MODULE_LOG_CATEGORY, "Module '%s' failed to initialize: %s",
                      entry->desc.name, result_to_string(result));

            for (uint32_t j = i; j-- > 0;) {
                ModuleEntry *rollback = &registry->entries[j];
                if (rollback->initialized && rollback->desc.on_shutdown != nullptr) {
                    rollback->desc.on_shutdown(rollback->desc.userdata);
                }
                rollback->initialized = false;
            }
            return result;
        }

        entry->initialized = true;
    }

    return RESULT_OK;
}

void module_registry_shutdown_all(ModuleRegistry *registry) {
    ASSERT(registry != nullptr);
    if (registry == nullptr) {
        return;
    }

    for (uint32_t i = registry->count; i-- > 0;) {
        ModuleEntry *entry = &registry->entries[i];
        if (entry->initialized) {
            LOG_DEBUG(MODULE_LOG_CATEGORY, "Shutting down module '%s'", entry->desc.name);
            if (entry->desc.on_shutdown != nullptr) {
                entry->desc.on_shutdown(entry->desc.userdata);
            }
            entry->initialized = false;
        }
    }
}

uint32_t module_registry_count(const ModuleRegistry *registry) {
    ASSERT(registry != nullptr);
    return registry != nullptr ? registry->count : 0;
}

const char *module_registry_name_at(const ModuleRegistry *registry, uint32_t index) {
    ASSERT(registry != nullptr);
    if (registry == nullptr || index >= registry->count) {
        return nullptr;
    }
    return registry->entries[index].desc.name;
}
