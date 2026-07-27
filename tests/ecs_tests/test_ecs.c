/*
 * ECS is a pure logic module (no GL, no window) - these tests run with
 * no video driver at all.
 */
#include "eisenfront/ecs.h"

#include "unity.h"

typedef struct Position {
    float x, y, z;
} Position;

typedef struct Velocity {
    float x, y, z;
} Velocity;

void setUp(void) {
}

void tearDown(void) {
}

static void test_entity_create_destroy_and_stale_id(void) {
    WorldDesc desc = world_desc_default();
    desc.max_entities = 4;
    World *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, world_create(&desc, &world));

    Entity a = ECS_INVALID_ENTITY;
    TEST_ASSERT_EQUAL(RESULT_OK, entity_create(world, &a));
    TEST_ASSERT_TRUE(entity_is_valid(world, a));

    TEST_ASSERT_EQUAL(RESULT_OK, entity_destroy(world, a));
    TEST_ASSERT_FALSE(entity_is_valid(world, a));

    Entity b = ECS_INVALID_ENTITY;
    TEST_ASSERT_EQUAL(RESULT_OK, entity_create(world, &b));
    TEST_ASSERT_NOT_EQUAL(a, b); /* generation differs even if the slot was reused */
    TEST_ASSERT_TRUE(entity_is_valid(world, b));

    world_destroy(world);
}

static void test_entity_capacity_exceeded(void) {
    WorldDesc desc = world_desc_default();
    desc.max_entities = 1;
    World *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, world_create(&desc, &world));

    Entity a = ECS_INVALID_ENTITY;
    Entity b = ECS_INVALID_ENTITY;
    TEST_ASSERT_EQUAL(RESULT_OK, entity_create(world, &a));
    TEST_ASSERT_EQUAL(RESULT_ERROR_CAPACITY_EXCEEDED, entity_create(world, &b));

    world_destroy(world);
}

static void test_add_get_has_remove_component(void) {
    World *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, world_create(nullptr, &world));

    ComponentTypeId position_type;
    TEST_ASSERT_EQUAL(RESULT_OK, world_register_component_type(world, sizeof(Position), &position_type));

    Entity entity;
    TEST_ASSERT_EQUAL(RESULT_OK, entity_create(world, &entity));
    TEST_ASSERT_FALSE(ecs_has_component(world, entity, position_type));

    const Position position = {1.0f, 2.0f, 3.0f};
    TEST_ASSERT_EQUAL(RESULT_OK, ecs_add_component(world, entity, position_type, &position));
    TEST_ASSERT_TRUE(ecs_has_component(world, entity, position_type));

    Position *stored = ecs_get_component(world, entity, position_type);
    TEST_ASSERT_NOT_NULL(stored);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, stored->x);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, stored->y);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, stored->z);

    TEST_ASSERT_EQUAL(RESULT_ERROR_ALREADY_EXISTS, ecs_add_component(world, entity, position_type, &position));

    TEST_ASSERT_EQUAL(RESULT_OK, ecs_remove_component(world, entity, position_type));
    TEST_ASSERT_FALSE(ecs_has_component(world, entity, position_type));
    TEST_ASSERT_NULL(ecs_get_component(world, entity, position_type));

    world_destroy(world);
}

static void test_swap_remove_preserves_other_entities_data(void) {
    World *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, world_create(nullptr, &world));

    ComponentTypeId position_type;
    TEST_ASSERT_EQUAL(RESULT_OK, world_register_component_type(world, sizeof(Position), &position_type));

    Entity entities[3];
    for (int i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL(RESULT_OK, entity_create(world, &entities[i]));
        const Position position = {(float)i, (float)i * 10.0f, (float)i * 100.0f};
        TEST_ASSERT_EQUAL(RESULT_OK, ecs_add_component(world, entities[i], position_type, &position));
    }

    /* Remove the middle one, which forces a swap with the last dense
     * entry - verifies the swap correctly repoints the moved entity's
     * sparse-set entry rather than corrupting it. */
    TEST_ASSERT_EQUAL(RESULT_OK, ecs_remove_component(world, entities[1], position_type));

    TEST_ASSERT_FALSE(ecs_has_component(world, entities[1], position_type));

    Position *first = ecs_get_component(world, entities[0], position_type);
    Position *third = ecs_get_component(world, entities[2], position_type);
    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_NOT_NULL(third);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, first->x);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, third->x);
    TEST_ASSERT_EQUAL_FLOAT(200.0f, third->z);

    world_destroy(world);
}

static void test_entity_destroy_removes_its_components_only(void) {
    World *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, world_create(nullptr, &world));

    ComponentTypeId position_type;
    TEST_ASSERT_EQUAL(RESULT_OK, world_register_component_type(world, sizeof(Position), &position_type));

    Entity a, b;
    TEST_ASSERT_EQUAL(RESULT_OK, entity_create(world, &a));
    TEST_ASSERT_EQUAL(RESULT_OK, entity_create(world, &b));
    const Position pa = {1.0f, 0.0f, 0.0f};
    const Position pb = {2.0f, 0.0f, 0.0f};
    TEST_ASSERT_EQUAL(RESULT_OK, ecs_add_component(world, a, position_type, &pa));
    TEST_ASSERT_EQUAL(RESULT_OK, ecs_add_component(world, b, position_type, &pb));

    TEST_ASSERT_EQUAL(RESULT_OK, entity_destroy(world, a));

    TEST_ASSERT_TRUE(ecs_has_component(world, b, position_type));
    Position *stored_b = ecs_get_component(world, b, position_type);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, stored_b->x);

    world_destroy(world);
}

typedef struct QueryCapture {
    int   call_count;
    float sum_positions_x;
} QueryCapture;

static void query_callback(Entity entity, void **components, void *userdata) {
    (void)entity;
    QueryCapture *capture = userdata;
    const Position *position = components[0];
    const Velocity *velocity = components[1];
    capture->call_count += 1;
    capture->sum_positions_x += position->x + velocity->x;
}

static void test_query_visits_only_entities_with_all_components(void) {
    World *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, world_create(nullptr, &world));

    ComponentTypeId position_type, velocity_type;
    TEST_ASSERT_EQUAL(RESULT_OK, world_register_component_type(world, sizeof(Position), &position_type));
    TEST_ASSERT_EQUAL(RESULT_OK, world_register_component_type(world, sizeof(Velocity), &velocity_type));

    Entity both, position_only, velocity_only;
    TEST_ASSERT_EQUAL(RESULT_OK, entity_create(world, &both));
    TEST_ASSERT_EQUAL(RESULT_OK, entity_create(world, &position_only));
    TEST_ASSERT_EQUAL(RESULT_OK, entity_create(world, &velocity_only));

    const Position position = {10.0f, 0.0f, 0.0f};
    const Velocity velocity = {5.0f, 0.0f, 0.0f};
    TEST_ASSERT_EQUAL(RESULT_OK, ecs_add_component(world, both, position_type, &position));
    TEST_ASSERT_EQUAL(RESULT_OK, ecs_add_component(world, both, velocity_type, &velocity));
    TEST_ASSERT_EQUAL(RESULT_OK, ecs_add_component(world, position_only, position_type, &position));
    TEST_ASSERT_EQUAL(RESULT_OK, ecs_add_component(world, velocity_only, velocity_type, &velocity));

    const EcsQuery query = {.types = {position_type, velocity_type}, .type_count = 2};
    QueryCapture   capture = {0};
    ecs_query_each(world, &query, query_callback, &capture);

    TEST_ASSERT_EQUAL(1, capture.call_count);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, capture.sum_positions_x);

    world_destroy(world);
}

static char g_system_order[8];
static int  g_system_order_count;
static float g_last_delta_time;

static void system_a(World *world, float delta_time, void *userdata) {
    (void)world;
    (void)userdata;
    g_system_order[g_system_order_count++] = 'A';
    g_last_delta_time = delta_time;
}

static void system_b(World *world, float delta_time, void *userdata) {
    (void)world;
    (void)delta_time;
    (void)userdata;
    g_system_order[g_system_order_count++] = 'B';
}

static void test_systems_run_in_registration_order(void) {
    g_system_order_count = 0;
    g_last_delta_time = 0.0f;

    World *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, world_create(nullptr, &world));

    const EcsSystemDesc desc_a = {.name = "a", .fn = system_a};
    const EcsSystemDesc desc_b = {.name = "b", .fn = system_b};
    TEST_ASSERT_EQUAL(RESULT_OK, world_register_system(world, &desc_a));
    TEST_ASSERT_EQUAL(RESULT_OK, world_register_system(world, &desc_b));

    world_update(world, 0.016f);

    TEST_ASSERT_EQUAL(2, g_system_order_count);
    TEST_ASSERT_EQUAL('A', g_system_order[0]);
    TEST_ASSERT_EQUAL('B', g_system_order[1]);
    TEST_ASSERT_EQUAL_FLOAT(0.016f, g_last_delta_time);

    world_destroy(world);
}

static void test_component_type_capacity_exceeded(void) {
    WorldDesc desc = world_desc_default();
    desc.max_component_types = 1;
    World *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, world_create(&desc, &world));

    ComponentTypeId a, b;
    TEST_ASSERT_EQUAL(RESULT_OK, world_register_component_type(world, sizeof(Position), &a));
    TEST_ASSERT_EQUAL(RESULT_ERROR_CAPACITY_EXCEEDED,
                       world_register_component_type(world, sizeof(Velocity), &b));

    world_destroy(world);
}

/* Regression: a world sized to its exact entity count (max_entities=1,
 * one entity actually created) must not miscount that entity's own
 * component pool slot as already occupied - entity slot indices run
 * 1..max_entities (0 reserved), so a pool sized max_entities exactly
 * is one short of covering that top index; sparse must be sized
 * max_entities+1. */
static void test_add_component_at_exact_entity_capacity(void) {
    WorldDesc desc = world_desc_default();
    desc.max_entities = 1;
    World *world = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, world_create(&desc, &world));

    ComponentTypeId position_type;
    TEST_ASSERT_EQUAL(RESULT_OK, world_register_component_type(world, sizeof(Position), &position_type));

    Entity entity = ECS_INVALID_ENTITY;
    TEST_ASSERT_EQUAL(RESULT_OK, entity_create(world, &entity));

    const Position position = {1.0f, 2.0f, 3.0f};
    TEST_ASSERT_EQUAL(RESULT_OK, ecs_add_component(world, entity, position_type, &position));
    TEST_ASSERT_TRUE(ecs_has_component(world, entity, position_type));

    const Position *stored = (const Position *)ecs_get_component(world, entity, position_type);
    TEST_ASSERT_NOT_NULL(stored);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, stored->x);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, stored->y);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, stored->z);

    world_destroy(world);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_entity_create_destroy_and_stale_id);
    RUN_TEST(test_entity_capacity_exceeded);
    RUN_TEST(test_add_get_has_remove_component);
    RUN_TEST(test_swap_remove_preserves_other_entities_data);
    RUN_TEST(test_entity_destroy_removes_its_components_only);
    RUN_TEST(test_query_visits_only_entities_with_all_components);
    RUN_TEST(test_systems_run_in_registration_order);
    RUN_TEST(test_component_type_capacity_exceeded);
    RUN_TEST(test_add_component_at_exact_entity_capacity);

    return UNITY_END();
}
