#include "eisenfront/ecs.h"

#include <stdlib.h>
#include <string.h>

#define ECS_ENTITY_INDEX_BITS 20u
#define ECS_ENTITY_INDEX_MASK ((1u << ECS_ENTITY_INDEX_BITS) - 1u)
#define ECS_MAX_GENERATION ((1u << (32u - ECS_ENTITY_INDEX_BITS)) - 1u)
#define ECS_SPARSE_NONE UINT32_MAX

typedef struct EntitySlot {
    uint32_t generation;
    bool     active;
} EntitySlot;

typedef struct ComponentPool {
    size_t    component_size;
    uint8_t  *dense_data;     /* max_entities * component_size bytes, tightly packed [0, count) */
    Entity   *dense_entities; /* dense_entities[i] owns dense_data + i * component_size */
    /* Indexed directly by entity slot index (1..max_entities, see
     * world->entities' own +1 comment), not by dense position -
     * max_entities+1 elements so slot index max_entities itself is
     * in bounds, exactly like world->entities. */
    uint32_t *sparse;
    uint32_t  count;
} ComponentPool;

struct World {
    EntitySlot *entities;
    uint32_t    max_entities;
    uint32_t   *free_indices;
    uint32_t    free_count;

    ComponentPool *pools;
    uint32_t       max_component_types;
    uint32_t       registered_component_types;

    EcsSystemDesc *systems;
    uint32_t       max_systems;
    uint32_t       system_count;
};

static uint32_t entity_index_of(Entity entity) {
    return entity & ECS_ENTITY_INDEX_MASK;
}

static uint32_t entity_generation_of(Entity entity) {
    return entity >> ECS_ENTITY_INDEX_BITS;
}

static Entity make_entity(uint32_t index, uint32_t generation) {
    return (Entity)((generation << ECS_ENTITY_INDEX_BITS) | (index & ECS_ENTITY_INDEX_MASK));
}

WorldDesc world_desc_default(void) {
    return (WorldDesc){
        .max_entities = 4096,
        .max_component_types = 32,
        .max_systems = 64,
    };
}

Result world_create(const WorldDesc *desc, World **out_world) {
    if (out_world == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    const WorldDesc effective = (desc != nullptr) ? *desc : world_desc_default();
    if (effective.max_entities == 0 || effective.max_entities > ECS_ENTITY_INDEX_MASK ||
        effective.max_component_types == 0 || effective.max_systems == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    World *world = malloc(sizeof(*world));
    if (world == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    /* +1: index 0 is reserved so ECS_INVALID_ENTITY (0) never addresses
     * a real slot - same convention as Scene's node indices. */
    world->entities = calloc((size_t)effective.max_entities + 1, sizeof(EntitySlot));
    world->free_indices = malloc(sizeof(uint32_t) * effective.max_entities);
    world->pools = calloc(effective.max_component_types, sizeof(ComponentPool));
    world->systems = malloc(sizeof(EcsSystemDesc) * effective.max_systems);
    if (world->entities == nullptr || world->free_indices == nullptr || world->pools == nullptr ||
        world->systems == nullptr) {
        free(world->entities);
        free(world->free_indices);
        free(world->pools);
        free(world->systems);
        free(world);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < effective.max_entities; ++i) {
        world->free_indices[i] = effective.max_entities - i;
    }
    world->free_count = effective.max_entities;
    world->max_entities = effective.max_entities;
    world->max_component_types = effective.max_component_types;
    world->registered_component_types = 0;
    world->max_systems = effective.max_systems;
    world->system_count = 0;

    *out_world = world;
    return RESULT_OK;
}

void world_destroy(World *world) {
    if (world == nullptr) {
        return;
    }
    for (uint32_t i = 0; i < world->registered_component_types; ++i) {
        free(world->pools[i].dense_data);
        free(world->pools[i].dense_entities);
        free(world->pools[i].sparse);
    }
    free(world->pools);
    free(world->entities);
    free(world->free_indices);
    free(world->systems);
    free(world);
}

Result world_register_component_type(World *world, size_t component_size,
                                      ComponentTypeId *out_type) {
    if (world == nullptr || component_size == 0 || out_type == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (world->registered_component_types >= world->max_component_types) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }

    const ComponentTypeId type = world->registered_component_types;
    ComponentPool        *pool = &world->pools[type];

    pool->component_size = component_size;
    pool->dense_data = malloc(component_size * world->max_entities);
    pool->dense_entities = malloc(sizeof(Entity) * world->max_entities);
    pool->sparse = malloc(sizeof(uint32_t) * ((size_t)world->max_entities + 1));
    if (pool->dense_data == nullptr || pool->dense_entities == nullptr || pool->sparse == nullptr) {
        free(pool->dense_data);
        free(pool->dense_entities);
        free(pool->sparse);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    for (uint32_t i = 0; i <= world->max_entities; ++i) {
        pool->sparse[i] = ECS_SPARSE_NONE;
    }
    pool->count = 0;

    world->registered_component_types += 1;
    *out_type = type;
    return RESULT_OK;
}

Result entity_create(World *world, Entity *out_entity) {
    if (world == nullptr || out_entity == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (world->free_count == 0) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }

    const uint32_t index = world->free_indices[--world->free_count];
    world->entities[index].active = true;

    *out_entity = make_entity(index, world->entities[index].generation);
    return RESULT_OK;
}

bool entity_is_valid(const World *world, Entity entity) {
    ASSERT(world != nullptr);
    if (world == nullptr || entity == ECS_INVALID_ENTITY) {
        return false;
    }
    const uint32_t index = entity_index_of(entity);
    if (index == 0 || index > world->max_entities) {
        return false;
    }
    return world->entities[index].active &&
           world->entities[index].generation == entity_generation_of(entity);
}

static Result remove_component_internal(World *world, Entity entity, ComponentTypeId type) {
    ComponentPool *pool = &world->pools[type];
    const uint32_t entity_index = entity_index_of(entity);
    const uint32_t dense_index = pool->sparse[entity_index];
    if (dense_index == ECS_SPARSE_NONE) {
        return RESULT_ERROR_NOT_FOUND;
    }

    const uint32_t last_index = pool->count - 1;
    if (dense_index != last_index) {
        memcpy(pool->dense_data + (size_t)dense_index * pool->component_size,
               pool->dense_data + (size_t)last_index * pool->component_size, pool->component_size);
        const Entity moved_entity = pool->dense_entities[last_index];
        pool->dense_entities[dense_index] = moved_entity;
        pool->sparse[entity_index_of(moved_entity)] = dense_index;
    }
    pool->sparse[entity_index] = ECS_SPARSE_NONE;
    pool->count -= 1;
    return RESULT_OK;
}

Result entity_destroy(World *world, Entity entity) {
    if (!entity_is_valid(world, entity)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const uint32_t entity_index = entity_index_of(entity);
    for (ComponentTypeId type = 0; type < world->registered_component_types; ++type) {
        if (world->pools[type].sparse[entity_index] != ECS_SPARSE_NONE) {
            remove_component_internal(world, entity, type);
        }
    }

    world->entities[entity_index].active = false;
    world->entities[entity_index].generation =
        (world->entities[entity_index].generation < ECS_MAX_GENERATION)
            ? world->entities[entity_index].generation + 1
            : 0;
    world->free_indices[world->free_count++] = entity_index;
    return RESULT_OK;
}

Result ecs_add_component(World *world, Entity entity, ComponentTypeId type,
                          const void *component_data) {
    if (!entity_is_valid(world, entity) || component_data == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (type >= world->registered_component_types) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    ComponentPool *pool = &world->pools[type];
    const uint32_t entity_index = entity_index_of(entity);
    if (pool->sparse[entity_index] != ECS_SPARSE_NONE) {
        return RESULT_ERROR_ALREADY_EXISTS;
    }

    const uint32_t dense_index = pool->count;
    memcpy(pool->dense_data + (size_t)dense_index * pool->component_size, component_data,
           pool->component_size);
    pool->dense_entities[dense_index] = entity;
    pool->sparse[entity_index] = dense_index;
    pool->count += 1;
    return RESULT_OK;
}

Result ecs_remove_component(World *world, Entity entity, ComponentTypeId type) {
    if (!entity_is_valid(world, entity)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (type >= world->registered_component_types) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    return remove_component_internal(world, entity, type);
}

bool ecs_has_component(const World *world, Entity entity, ComponentTypeId type) {
    if (!entity_is_valid(world, entity) || type >= world->registered_component_types) {
        return false;
    }
    return world->pools[type].sparse[entity_index_of(entity)] != ECS_SPARSE_NONE;
}

void *ecs_get_component(const World *world, Entity entity, ComponentTypeId type) {
    if (!entity_is_valid(world, entity) || type >= world->registered_component_types) {
        return nullptr;
    }
    const ComponentPool *pool = &world->pools[type];
    const uint32_t        dense_index = pool->sparse[entity_index_of(entity)];
    if (dense_index == ECS_SPARSE_NONE) {
        return nullptr;
    }
    return pool->dense_data + (size_t)dense_index * pool->component_size;
}

void ecs_query_each(World *world, const EcsQuery *query, EcsQueryFn fn, void *userdata) {
    ASSERT(world != nullptr);
    ASSERT(query != nullptr);
    ASSERT(fn != nullptr);
    if (world == nullptr || query == nullptr || fn == nullptr || query->type_count == 0) {
        return;
    }

    /* Iterate whichever required pool currently has the fewest entries:
     * that bounds the whole query at O(smallest pool), and every other
     * required type is then just an O(1) sparse-set membership check
     * per candidate. */
    uint32_t driver_slot = 0;
    uint32_t smallest_count = UINT32_MAX;
    for (uint32_t i = 0; i < query->type_count; ++i) {
        const uint32_t count = world->pools[query->types[i]].count;
        if (count < smallest_count) {
            smallest_count = count;
            driver_slot = i;
        }
    }

    const ComponentPool *driver_pool = &world->pools[query->types[driver_slot]];
    void                 *components[ECS_MAX_QUERY_COMPONENTS];

    for (uint32_t i = 0; i < driver_pool->count; ++i) {
        const Entity candidate = driver_pool->dense_entities[i];
        bool         matches_all = true;
        for (uint32_t t = 0; t < query->type_count; ++t) {
            void *component = ecs_get_component(world, candidate, query->types[t]);
            if (component == nullptr) {
                matches_all = false;
                break;
            }
            components[t] = component;
        }
        if (matches_all) {
            fn(candidate, components, userdata);
        }
    }
}

Result world_register_system(World *world, const EcsSystemDesc *desc) {
    if (world == nullptr || desc == nullptr || desc->fn == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (world->system_count >= world->max_systems) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }
    world->systems[world->system_count] = *desc;
    world->system_count += 1;
    return RESULT_OK;
}

void world_update(World *world, float delta_time) {
    ASSERT(world != nullptr);
    if (world == nullptr) {
        return;
    }
    for (uint32_t i = 0; i < world->system_count; ++i) {
        const EcsSystemDesc *system = &world->systems[i];
        system->fn(world, delta_time, system->userdata);
    }
}
