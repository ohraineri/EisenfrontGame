/*
 * Runs against SDL3's "offscreen" video driver (see CMakeLists.txt), a
 * real EGL/NVIDIA OpenGL 4.6 context in this environment - shaders here
 * are really compiled and linked, not mocked. SHADER_TEST_DIR is a
 * CMake compile definition pointing at tests/shader_tests/shaders.
 */
#include "eisenfront/shader.h"

#include "unity.h"

#include <stdio.h>
#include <string.h>

#ifndef SHADER_TEST_DIR
#    error "SHADER_TEST_DIR must be defined by CMake"
#endif

static Window          *g_window;
static GraphicsContext *g_context;

static void path_for(const char *filename, char *out_buffer, size_t capacity) {
    snprintf(out_buffer, capacity, "%s/%s", SHADER_TEST_DIR, filename);
}

void setUp(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const WindowDesc window_desc = {
        .title = "shader-test",
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
}

void tearDown(void) {
    shader_system_shutdown();
    graphics_context_destroy(g_context);
    g_context = nullptr;
    window_destroy(g_window);
    g_window = nullptr;
    window_shutdown();
}

static void test_load_valid_program(void) {
    char vertex_path[512], fragment_path[512];
    path_for("triangle.vert", vertex_path, sizeof(vertex_path));
    path_for("triangle.frag", fragment_path, sizeof(fragment_path));

    const ShaderProgramDesc desc = {.vertex_path = vertex_path, .fragment_path = fragment_path};
    ShaderProgram *program = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, shader_program_load(&desc, &program));
    TEST_ASSERT_NOT_NULL(program);
    TEST_ASSERT_TRUE(shader_program_get_gl_handle(program) != 0);
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    shader_program_release(program);
}

static void test_duplicate_load_returns_cached_program(void) {
    char vertex_path[512], fragment_path[512];
    path_for("triangle.vert", vertex_path, sizeof(vertex_path));
    path_for("triangle.frag", fragment_path, sizeof(fragment_path));
    const ShaderProgramDesc desc = {.vertex_path = vertex_path, .fragment_path = fragment_path};

    ShaderProgram *first = nullptr;
    ShaderProgram *second = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, shader_program_load(&desc, &first));
    TEST_ASSERT_EQUAL(RESULT_OK, shader_program_load(&desc, &second));
    TEST_ASSERT_EQUAL_PTR(first, second);

    shader_program_release(first);
    shader_program_release(second);
}

static void test_reflection_reports_uniforms_and_attributes(void) {
    char vertex_path[512], fragment_path[512];
    path_for("triangle.vert", vertex_path, sizeof(vertex_path));
    path_for("triangle.frag", fragment_path, sizeof(fragment_path));
    const ShaderProgramDesc desc = {.vertex_path = vertex_path, .fragment_path = fragment_path};

    ShaderProgram *program = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, shader_program_load(&desc, &program));

    TEST_ASSERT_EQUAL(3, shader_program_uniform_count(program));
    TEST_ASSERT_TRUE(shader_program_get_uniform_location(program, "uModel") >= 0);
    TEST_ASSERT_TRUE(shader_program_get_uniform_location(program, "uTint") >= 0);
    TEST_ASSERT_TRUE(shader_program_get_uniform_location(program, "uAlpha") >= 0);
    TEST_ASSERT_EQUAL(-1, shader_program_get_uniform_location(program, "uDoesNotExist"));

    TEST_ASSERT_EQUAL(1, shader_program_attribute_count(program));
    ShaderAttributeInfo attribute_info;
    TEST_ASSERT_TRUE(shader_program_get_attribute_info(program, 0, &attribute_info));
    TEST_ASSERT_EQUAL_STRING("aPos", attribute_info.name);

    shader_program_release(program);
}

static void test_uniform_setters_produce_no_gl_error(void) {
    char vertex_path[512], fragment_path[512];
    path_for("triangle.vert", vertex_path, sizeof(vertex_path));
    path_for("triangle.frag", fragment_path, sizeof(fragment_path));
    const ShaderProgramDesc desc = {.vertex_path = vertex_path, .fragment_path = fragment_path};

    ShaderProgram *program = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, shader_program_load(&desc, &program));

    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    shader_set_uniform_mat4(program, "uModel", identity);
    shader_set_uniform_3f(program, "uTint", 1.0f, 0.5f, 0.25f);
    shader_set_uniform_1f(program, "uAlpha", 1.0f);
    /* Nonexistent uniform names resolve to location -1 and must be a
     * silent no-op rather than a GL error. */
    shader_set_uniform_1i(program, "uDoesNotExist", 7);

    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    shader_program_release(program);
}

static void test_broken_shader_fails_to_load(void) {
    char vertex_path[512], broken_path[512];
    path_for("triangle.vert", vertex_path, sizeof(vertex_path));
    path_for("broken.frag", broken_path, sizeof(broken_path));
    const ShaderProgramDesc desc = {.vertex_path = vertex_path, .fragment_path = broken_path};

    ShaderProgram *program = nullptr;
    TEST_ASSERT_EQUAL(RESULT_ERROR_PLATFORM, shader_program_load(&desc, &program));
}

static const char *k_valid_fragment_source = "#version 460 core\n"
                                              "out vec4 FragColor;\n"
                                              "void main() { FragColor = vec4(1.0); }\n";
static const char *k_broken_fragment_source = "#version 460 core\n"
                                               "out vec4 FragColor;\n"
                                               "void main() { FragColor = still not glsl; }\n";
static const char *k_valid_fragment_source_v2 = "#version 460 core\n"
                                                 "out vec4 FragColor;\n"
                                                 "uniform float uExtra;\n"
                                                 "void main() { FragColor = vec4(uExtra); }\n";

static void test_reload_keeps_last_good_program_on_failure_then_recovers(void) {
    char vertex_path[512], reload_path[512];
    path_for("triangle.vert", vertex_path, sizeof(vertex_path));
    path_for("reload_target.frag", reload_path, sizeof(reload_path));

    TEST_ASSERT_EQUAL(RESULT_OK, file_write_all(reload_path, k_valid_fragment_source,
                                                 strlen(k_valid_fragment_source)));

    const ShaderProgramDesc desc = {.vertex_path = vertex_path, .fragment_path = reload_path};
    ShaderProgram *program = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, shader_program_load(&desc, &program));
    const uint32_t original_handle = shader_program_get_gl_handle(program);
    /* triangle.vert's own uModel/uTint uniforms are active regardless of
     * which fragment source is linked in; what this test actually cares
     * about is uExtra, which only the "v2" fragment source below
     * declares - so that is what gets asserted, rather than a total
     * count that would also depend on the GLSL compiler's dead-code
     * elimination of the unread vTint varying. */
    TEST_ASSERT_EQUAL(-1, shader_program_get_uniform_location(program, "uExtra"));

    /* Break it: reload must fail and leave the working program alone. */
    TEST_ASSERT_EQUAL(RESULT_OK, file_write_all(reload_path, k_broken_fragment_source,
                                                 strlen(k_broken_fragment_source)));
    TEST_ASSERT_EQUAL(RESULT_ERROR_PLATFORM, shader_program_reload(program));
    TEST_ASSERT_EQUAL(original_handle, shader_program_get_gl_handle(program));
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    /* Fix it with a change that is observable through reflection: reload
     * must actually pick up the new source, not silently keep the old
     * compiled result. */
    TEST_ASSERT_EQUAL(RESULT_OK, file_write_all(reload_path, k_valid_fragment_source_v2,
                                                 strlen(k_valid_fragment_source_v2)));
    TEST_ASSERT_EQUAL(RESULT_OK, shader_program_reload(program));
    TEST_ASSERT_TRUE(shader_program_get_uniform_location(program, "uExtra") >= 0);

    shader_program_release(program);
    file_delete(reload_path);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_load_valid_program);
    RUN_TEST(test_duplicate_load_returns_cached_program);
    RUN_TEST(test_reflection_reports_uniforms_and_attributes);
    RUN_TEST(test_uniform_setters_produce_no_gl_error);
    RUN_TEST(test_broken_shader_fails_to_load);
    RUN_TEST(test_reload_keeps_last_good_program_on_failure_then_recovers);

    return UNITY_END();
}
