/*
 * Runs against SDL3's "offscreen" video driver (see CMakeLists.txt), a
 * real EGL/NVIDIA OpenGL 4.6 context in this environment - real shader,
 * texture and UBO objects, not mocks.
 */
#include "eisenfront/material.h"

#include "unity.h"

#include <stdio.h>

#ifndef MATERIAL_TEST_SHADER_DIR
#    error "MATERIAL_TEST_SHADER_DIR must be defined by CMake"
#endif
#ifndef MATERIAL_TEST_TEXTURE_DIR
#    error "MATERIAL_TEST_TEXTURE_DIR must be defined by CMake"
#endif

static Window          *g_window;
static GraphicsContext *g_context;
static ShaderProgram   *g_shader;

static void path_in(const char *dir, const char *filename, char *out_buffer, size_t capacity) {
    snprintf(out_buffer, capacity, "%s/%s", dir, filename);
}

void setUp(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const WindowDesc window_desc = {
        .title = "material-test",
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
    path_in(MATERIAL_TEST_SHADER_DIR, "triangle.vert", vertex_path, sizeof(vertex_path));
    path_in(MATERIAL_TEST_SHADER_DIR, "triangle.frag", fragment_path, sizeof(fragment_path));
    const ShaderProgramDesc desc = {.vertex_path = vertex_path, .fragment_path = fragment_path};
    TEST_ASSERT_EQUAL(RESULT_OK, shader_program_load(&desc, &g_shader));
}

void tearDown(void) {
    shader_program_release(g_shader);
    g_shader = nullptr;
    material_system_shutdown();
    texture_system_shutdown();
    shader_system_shutdown();
    graphics_context_destroy(g_context);
    g_context = nullptr;
    window_destroy(g_window);
    g_window = nullptr;
    window_shutdown();
}

static uint32_t bound_texture_for_unit(uint32_t unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    GLint id = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &id);
    return (uint32_t)id;
}

static void read_material_ubo(float out_base_color[4]) {
    GLint ubo = 0;
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, MATERIAL_UBO_BINDING, &ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, (GLuint)ubo);
    glGetBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(float) * 4, out_base_color);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

static void test_material_create_and_bind_uses_fallback_textures(void) {
    const MaterialDesc desc = {
        .shader = g_shader,
        .textures = {0},
        .params = material_params_default(),
    };
    Material *material = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, material_create(&desc, &material));

    material_bind(material);
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    /* No textures were supplied, so every unit must still have *some*
     * valid texture bound (the shared fallback), not unit 0/unbound. */
    TEST_ASSERT_TRUE(bound_texture_for_unit(MATERIAL_TEXTURE_UNIT_DIFFUSE) != 0);
    TEST_ASSERT_TRUE(bound_texture_for_unit(MATERIAL_TEXTURE_UNIT_NORMAL) != 0);
    TEST_ASSERT_TRUE(bound_texture_for_unit(MATERIAL_TEXTURE_UNIT_AO) != 0);

    material_release(material);
}

static void test_material_uses_explicit_diffuse_texture(void) {
    char path[512];
    path_in(MATERIAL_TEST_TEXTURE_DIR, "red_4x4.bmp", path, sizeof(path));
    Texture *diffuse = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, texture_load_2d(path, nullptr, &diffuse));

    const MaterialDesc desc = {
        .shader = g_shader,
        .textures = {.diffuse = diffuse},
        .params = material_params_default(),
    };
    Material *material = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, material_create(&desc, &material));

    material_bind(material);
    TEST_ASSERT_EQUAL(texture_get_gl_handle(diffuse), bound_texture_for_unit(MATERIAL_TEXTURE_UNIT_DIFFUSE));

    material_release(material);
    texture_release(diffuse);
}

static void test_material_params_land_in_the_ubo(void) {
    MaterialParams params = material_params_default();
    params.base_color[0] = 0.25f;
    params.base_color[1] = 0.5f;
    params.base_color[2] = 0.75f;
    params.base_color[3] = 1.0f;

    const MaterialDesc desc = {.shader = g_shader, .textures = {0}, .params = params};
    Material *material = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, material_create(&desc, &material));
    material_bind(material);

    float readback[4] = {0};
    read_material_ubo(readback);
    TEST_ASSERT_EQUAL_FLOAT(0.25f, readback[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, readback[1]);
    TEST_ASSERT_EQUAL_FLOAT(0.75f, readback[2]);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, readback[3]);

    material_release(material);
}

static void test_material_set_params_updates_the_ubo(void) {
    const MaterialDesc desc = {.shader = g_shader, .textures = {0}, .params = material_params_default()};
    Material *material = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, material_create(&desc, &material));

    MaterialParams updated = material_params_default();
    updated.base_color[0] = 0.1f;
    updated.base_color[1] = 0.2f;
    updated.base_color[2] = 0.3f;
    updated.base_color[3] = 0.4f;
    material_set_params(material, &updated);

    material_bind(material);
    float readback[4] = {0};
    read_material_ubo(readback);
    TEST_ASSERT_EQUAL_FLOAT(0.1f, readback[0]);
    TEST_ASSERT_EQUAL_FLOAT(0.4f, readback[3]);

    material_release(material);
}

static void test_instance_clones_base_then_diverges_independently(void) {
    MaterialParams base_params = material_params_default();
    base_params.base_color[0] = 0.9f;
    base_params.base_color[1] = 0.9f;
    base_params.base_color[2] = 0.9f;
    base_params.base_color[3] = 1.0f;

    const MaterialDesc desc = {.shader = g_shader, .textures = {0}, .params = base_params};
    Material *base = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, material_create(&desc, &base));

    MaterialInstance *instance = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, material_instance_create(base, &instance));

    /* Freshly cloned: instance's UBO must start equal to the base's. */
    material_instance_bind(instance);
    float instance_readback[4] = {0};
    read_material_ubo(instance_readback);
    TEST_ASSERT_EQUAL_FLOAT(0.9f, instance_readback[0]);

    /* Diverge the instance only. */
    MaterialParams tint = base_params;
    tint.base_color[0] = 0.1f;
    material_instance_set_params(instance, &tint);

    material_instance_bind(instance);
    read_material_ubo(instance_readback);
    TEST_ASSERT_EQUAL_FLOAT(0.1f, instance_readback[0]);

    /* The base must be untouched by the instance's divergence. */
    material_bind(base);
    float base_readback[4] = {0};
    read_material_ubo(base_readback);
    TEST_ASSERT_EQUAL_FLOAT(0.9f, base_readback[0]);

    material_instance_release(instance);
    material_release(base);
}

static void test_instance_texture_override_does_not_affect_base(void) {
    char red_path[512], blue_path[512];
    path_in(MATERIAL_TEST_TEXTURE_DIR, "red_4x4.bmp", red_path, sizeof(red_path));
    path_in(MATERIAL_TEST_TEXTURE_DIR, "blue_4x4.bmp", blue_path, sizeof(blue_path));
    Texture *red = nullptr;
    Texture *blue = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, texture_load_2d(red_path, nullptr, &red));
    TEST_ASSERT_EQUAL(RESULT_OK, texture_load_2d(blue_path, nullptr, &blue));

    const MaterialDesc desc = {
        .shader = g_shader, .textures = {.diffuse = red}, .params = material_params_default()};
    Material *base = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, material_create(&desc, &base));

    MaterialInstance *instance = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, material_instance_create(base, &instance));

    const MaterialTextures override = {.diffuse = blue};
    material_instance_set_textures(instance, &override);

    material_instance_bind(instance);
    TEST_ASSERT_EQUAL(texture_get_gl_handle(blue), bound_texture_for_unit(MATERIAL_TEXTURE_UNIT_DIFFUSE));

    material_bind(base);
    TEST_ASSERT_EQUAL(texture_get_gl_handle(red), bound_texture_for_unit(MATERIAL_TEXTURE_UNIT_DIFFUSE));

    material_instance_release(instance);
    material_release(base);
    texture_release(red);
    texture_release(blue);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_material_create_and_bind_uses_fallback_textures);
    RUN_TEST(test_material_uses_explicit_diffuse_texture);
    RUN_TEST(test_material_params_land_in_the_ubo);
    RUN_TEST(test_material_set_params_updates_the_ubo);
    RUN_TEST(test_instance_clones_base_then_diverges_independently);
    RUN_TEST(test_instance_texture_override_does_not_affect_base);

    return UNITY_END();
}
