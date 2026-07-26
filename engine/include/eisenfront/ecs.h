/*
 * ECS - a sparse-set entity component system: composition over
 * inheritance, entities are nothing but an ID, components are plain
 * data, systems are plain functions.
 *
 * Cache locality: every component type gets its own ComponentPool, a
 * single contiguous array of exactly that type's bytes (a "structure of
 * arrays" layout) - not one array of "entity structs" each holding every
 * component type inline ("array of structures"). A system that only
 * touches Position and Velocity, for example, streams through two tight
 * arrays of just those bytes; the CPU's cache lines fill entirely with
 * data that system actually uses, and prefetching across a linear scan
 * is trivial for the hardware to predict. An array-of-structures layout
 * would pull in every OTHER component on the same entity (health, mesh
 * handles, AI state, ...) with every cache line, wasting most of the
 * line's bytes on data the running system never touches. Within a pool,
 * the dense array is kept fully packed (see "sparse sets" below): there
 * are never gaps to skip over, so iterating count active components is
 * exactly count sequential reads, not count-plus-holes.
 *
 * Sparse sets: each ComponentPool is two arrays plus one lookup table.
 * The dense array holds component data, tightly packed, in whatever
 * order components were added - this is what systems iterate. A
 * parallel dense_entities array records which Entity owns
 * dense[i]. The sparse array is indexed directly by an entity's slot
 * index and stores where that entity's component lives in the dense
 * array (or "not present"). Add/remove/has/get are therefore all O(1):
 * add appends to the dense array's end and records the mapping; remove
 * swaps the dense array's last element into the removed slot (updating
 * that moved entity's sparse entry) and shrinks the count by one - so
 * removal is O(1) but does not preserve dense array order, which is the
 * standard, expected sparse-set trade-off. A consequence: any pointer
 * returned by ecs_get_component() is invalidated by the next
 * ecs_remove_component() or entity_destroy() call that touches the same
 * pool (that swap can move a different entity's component to a new
 * index) - never hold one across such a call.
 *
 * Entities are generational handles (the same index+generation scheme
 * as Scene's SceneNodeId - see scene.h): the low 20 bits are a slot
 * index, the high 12 bits are that slot's generation counter, so a
 * stale Entity from before a destroy is detected rather than
 * accidentally referring to whatever entity was created in the same
 * slot afterward.
 *
 * Composition only, no inheritance: a "kind of thing" in this ECS is
 * just whichever combination of components an entity happens to carry,
 * decided at runtime by calling ecs_add_component()/ecs_remove_component() -
 * there is no type hierarchy to design up front, and nothing stops an
 * entity from mixing components in ways a class hierarchy would have
 * forced you to choose between.
 *
 * Systems and queries are deliberately separate mechanisms that
 * compose: a system is just a plain function called once per
 * world_update() tick, in registration order; a query
 * (ecs_query_each()) is how that function efficiently finds "every
 * entity with components A and B" by iterating the smallest of the
 * requested pools and checking O(1) sparse-set membership against the
 * rest, rather than baking a fixed query into system registration
 * metadata.
 *
 * Memory trade-off: every registered component pool is allocated at
 * WorldDesc.max_entities capacity up front (no reallocation, ever - the
 * same "fixed capacity, no realloc-invalidation" choice this engine
 * makes throughout, e.g. Core's log sinks, Shader's/Texture's caches).
 * That means a rarely-used component type still costs
 * component_size * max_entities bytes, whether or not many entities
 * actually have it; a production engine chasing tighter memory would
 * chunk-allocate pools instead. This engine takes the simpler, always-
 * correct option and documents the cost rather than hiding it.
 *
 * Dependencies: Core only.
 */
#ifndef EISENFRONT_ECS_H
#define EISENFRONT_ECS_H

#include "core.h"

typedef uint32_t Entity;
#define ECS_INVALID_ENTITY ((Entity)0)

typedef uint32_t ComponentTypeId;
#define ECS_INVALID_COMPONENT_TYPE ((ComponentTypeId)UINT32_MAX)

typedef struct WorldDesc {
    uint32_t max_entities;
    uint32_t max_component_types;
    uint32_t max_systems;
} WorldDesc;

ENGINE_API WorldDesc world_desc_default(void);

typedef struct World World;

ENGINE_API Result world_create(const WorldDesc *desc, World **out_world);
ENGINE_API void   world_destroy(World *world);

ENGINE_API Result world_register_component_type(World *world, size_t component_size,
                                                  ComponentTypeId *out_type);

ENGINE_API Result entity_create(World *world, Entity *out_entity);
/* Removes entity from every component pool it belongs to, then frees
 * its slot. */
ENGINE_API Result entity_destroy(World *world, Entity entity);
ENGINE_API bool   entity_is_valid(const World *world, Entity entity);

/* component_data is copied (component_size bytes, from registration) -
 * the caller's buffer is not retained. */
ENGINE_API Result ecs_add_component(World *world, Entity entity, ComponentTypeId type,
                                     const void *component_data);
ENGINE_API Result ecs_remove_component(World *world, Entity entity, ComponentTypeId type);
ENGINE_API bool   ecs_has_component(const World *world, Entity entity, ComponentTypeId type);
/* nullptr if entity does not have this component. See the file header
 * comment on when this pointer is invalidated. */
ENGINE_API void *ecs_get_component(const World *world, Entity entity, ComponentTypeId type);

#define ECS_MAX_QUERY_COMPONENTS 8u

typedef struct EcsQuery {
    ComponentTypeId types[ECS_MAX_QUERY_COMPONENTS];
    uint32_t        type_count;
} EcsQuery;

/* components[i] is the pointer for query->types[i], in the same order -
 * exactly what ecs_get_component(world, entity, query->types[i]) would
 * have returned, just already resolved once per matching entity instead
 * of once per (entity, type) pair. */
typedef void (*EcsQueryFn)(Entity entity, void **components, void *userdata);

ENGINE_API void ecs_query_each(World *world, const EcsQuery *query, EcsQueryFn fn, void *userdata);

typedef void (*EcsSystemFn)(World *world, float delta_time, void *userdata);

typedef struct EcsSystemDesc {
    const char *name;
    EcsSystemFn fn;
    void       *userdata;
} EcsSystemDesc;

ENGINE_API Result world_register_system(World *world, const EcsSystemDesc *desc);
/* Runs every registered system once, in registration order. */
ENGINE_API void   world_update(World *world, float delta_time);

#endif /* EISENFRONT_ECS_H */
