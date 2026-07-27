/*
 * Runs against SDL3's "offscreen" video driver (see CMakeLists.txt), a
 * real EGL/NVIDIA OpenGL 4.6 context in this environment - a real
 * shader, real materials, and a real PhysicsWorld with the actual
 * bodies outpost_level_create() builds, not a hand-rolled fixture. That
 * matters here specifically because outpost_level_surface_type_for_body()
 * exists to catch a material/SurfaceType mismatch introduced while
 * editing outpost_scene.c - a synthetic OutpostLevel wouldn't exercise
 * the real tagging at each add_box() call site.
 */
#include "outpost_scene.h"

#include "unity.h"

#include <stdio.h>

#ifndef OUTPOST_SCENE_TEST_SHADER_DIR
#    error "OUTPOST_SCENE_TEST_SHADER_DIR must be defined by CMake"
#endif

static Window          *g_window;
static GraphicsContext *g_context;
static ShaderProgram   *g_shader;
static PhysicsWorld     *g_physics_world;
static OutpostLevel      g_level;

void setUp(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const WindowDesc window_desc = {
        .title = "outpost-scene-test",
        .width = 320,
        .height = 240,
        .mode = WINDOW_MODE_WINDOWED,
        .resizable = false,
        .monitor_index = -1,
    };
    TEST_ASSERT_EQUAL(RESULT_OK, window_create(&window_desc, &g_window));

    GraphicsContextDesc context_desc = graphics_context_desc_default();
    context_desc.vsync = VSYNC_OFF;
    TEST_ASSERT_EQUAL(RESULT_OK, graphics_context_create(g_window, &context_desc, &g_context));

    TEST_ASSERT_EQUAL(RESULT_OK, shader_system_init());
    TEST_ASSERT_EQUAL(RESULT_OK, texture_system_init());
    TEST_ASSERT_EQUAL(RESULT_OK, material_system_init());

    char vertex_path[512], fragment_path[512];
    snprintf(vertex_path, sizeof(vertex_path), "%s/lit.vert", OUTPOST_SCENE_TEST_SHADER_DIR);
    snprintf(fragment_path, sizeof(fragment_path), "%s/lit.frag", OUTPOST_SCENE_TEST_SHADER_DIR);
    const ShaderProgramDesc shader_desc = {
        .vertex_path = vertex_path, .fragment_path = fragment_path, .geometry_path = nullptr};
    TEST_ASSERT_EQUAL(RESULT_OK, shader_program_load(&shader_desc, &g_shader));

    PhysicsWorldDesc physics_desc = physics_world_desc_default();
    TEST_ASSERT_EQUAL(RESULT_OK, physics_world_create(&physics_desc, &g_physics_world));

    TEST_ASSERT_EQUAL(RESULT_OK, outpost_level_create(g_shader, g_physics_world, &g_level));
}

void tearDown(void) {
    outpost_level_destroy(&g_level, g_physics_world);
    physics_world_destroy(g_physics_world);
    shader_program_release(g_shader);
    material_system_shutdown();
    texture_system_shutdown();
    shader_system_shutdown();
    graphics_context_destroy(g_context);
    window_destroy(g_window);
    window_shutdown();
}

static void test_lookup_matches_every_tracked_object_own_surface_type(void) {
    TEST_ASSERT_GREATER_THAN(0u, g_level.object_count);
    for (uint32_t i = 0; i < g_level.object_count; ++i) {
        const StaticObject *object = &g_level.objects[i];
        TEST_ASSERT_EQUAL(object->surface_type, outpost_level_surface_type_for_body(&g_level, object->body));
    }
}

static void test_level_tags_more_than_one_surface_type(void) {
    /* The level is meant to be data-driven and varied (gravel
     * perimeter, sand sandbags, wood watchtower/crates, metal
     * shed/barrels, concrete building) - if this collapses to one
     * value, footstep audio silently stopped varying. */
    bool seen[SURFACE_TYPE_COUNT] = {0};
    for (uint32_t i = 0; i < g_level.object_count; ++i) {
        seen[g_level.objects[i].surface_type] = true;
    }
    uint32_t distinct_count = 0;
    for (uint32_t i = 0; i < SURFACE_TYPE_COUNT; ++i) {
        if (seen[i]) {
            ++distinct_count;
        }
    }
    TEST_ASSERT_GREATER_THAN(1u, distinct_count);
}

static void test_watchtower_platform_is_tagged_wood(void) {
    bool found_wood = false;
    for (uint32_t i = 0; i < g_level.object_count; ++i) {
        if (g_level.objects[i].material == g_level.watchtower_material) {
            TEST_ASSERT_EQUAL(SURFACE_TYPE_WOOD, g_level.objects[i].surface_type);
            found_wood = true;
        }
    }
    TEST_ASSERT_TRUE(found_wood);
}

static void test_lookup_defaults_to_soil_for_an_untracked_body(void) {
    /* Covers both "no static object owns this body" and, by extension,
     * the level's untracked ground plane (created separately by
     * main.c, never part of objects[]). */
    TEST_ASSERT_EQUAL(SURFACE_TYPE_SOIL,
                       outpost_level_surface_type_for_body(&g_level, (BodyId)0xFFFFFFFFu));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_lookup_matches_every_tracked_object_own_surface_type);
    RUN_TEST(test_level_tags_more_than_one_surface_type);
    RUN_TEST(test_watchtower_platform_is_tagged_wood);
    RUN_TEST(test_lookup_defaults_to_soil_for_an_untracked_body);

    return UNITY_END();
}
