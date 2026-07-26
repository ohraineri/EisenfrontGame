/*
 * Runs against SDL3's "offscreen" video driver (see CMakeLists.txt), which
 * in this environment resolves to a real EGL/NVIDIA OpenGL 4.6 context -
 * draw calls in these tests are real GL draw calls, not mocks. The
 * minimal shader/VAO helpers below stand in for the not-yet-built
 * Shader/Buffers modules; Renderer only needs raw GL object names.
 */
#include "eisenfront/renderer.h"

#include "unity.h"

static Window          *g_window;
static GraphicsContext *g_context;

void setUp(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const WindowDesc window_desc = {
        .title = "renderer-test",
        .width = 640,
        .height = 480,
        .mode = WINDOW_MODE_WINDOWED,
        .resizable = false,
        .monitor_index = -1,
    };
    TEST_ASSERT_EQUAL(RESULT_OK, window_create(&window_desc, &g_window));

    GraphicsContextDesc context_desc = graphics_context_desc_default();
    context_desc.vsync = VSYNC_OFF;
    TEST_ASSERT_EQUAL(RESULT_OK, graphics_context_create(g_window, &context_desc, &g_context));
}

void tearDown(void) {
    graphics_context_destroy(g_context);
    g_context = nullptr;
    window_destroy(g_window);
    g_window = nullptr;
    window_shutdown();
}

static GLuint compile_shader(GLenum stage, const char *source) {
    GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    TEST_ASSERT_EQUAL(GL_TRUE, status);
    return shader;
}

static GLuint create_triangle_program(void) {
    static const char *vertex_source = "#version 460 core\n"
                                        "layout(location = 0) in vec2 aPos;\n"
                                        "void main() { gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    static const char *fragment_source = "#version 460 core\n"
                                          "out vec4 FragColor;\n"
                                          "void main() { FragColor = vec4(1.0); }\n";

    const GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
    const GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    TEST_ASSERT_EQUAL(GL_TRUE, status);

    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return program;
}

static GLuint create_triangle_vao(void) {
    static const float vertices[6] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f};

    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    return vao;
}

static void test_frame_with_one_draw_updates_stats(void) {
    Renderer *renderer = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, renderer_create(g_context, nullptr, &renderer));

    const GLuint program = create_triangle_program();
    const GLuint vao = create_triangle_vao();

    renderer_begin_frame(renderer);
    renderer_clear(renderer, nullptr);
    renderer_set_viewport(renderer, 0, 0, 640, 480);

    const RenderCommand command = {
        .vertex_array = vao,
        .shader_program = program,
        .topology = PRIMITIVE_TOPOLOGY_TRIANGLES,
        .indexed = false,
        .vertex_count = 3,
        .instance_count = 1,
        .sort_key = 0,
    };
    TEST_ASSERT_EQUAL(RESULT_OK, renderer_submit(renderer, &command));
    renderer_end_frame(renderer);

    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    RendererStats stats;
    renderer_get_stats(renderer, &stats);
    TEST_ASSERT_EQUAL(1, stats.draw_calls);
    TEST_ASSERT_EQUAL(1, stats.triangles);
    TEST_ASSERT_EQUAL(1, stats.frame_count);
    TEST_ASSERT_TRUE(stats.state_changes >= 2); /* program bind + VAO bind */

    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    renderer_destroy(renderer);
}

static void test_stats_reset_each_frame(void) {
    Renderer *renderer = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, renderer_create(g_context, nullptr, &renderer));

    const GLuint program = create_triangle_program();
    const GLuint vao = create_triangle_vao();
    const RenderCommand command = {
        .vertex_array = vao,
        .shader_program = program,
        .topology = PRIMITIVE_TOPOLOGY_TRIANGLES,
        .vertex_count = 3,
        .instance_count = 1,
    };

    renderer_begin_frame(renderer);
    TEST_ASSERT_EQUAL(RESULT_OK, renderer_submit(renderer, &command));
    renderer_end_frame(renderer);

    renderer_begin_frame(renderer); /* no submissions this frame */
    renderer_end_frame(renderer);

    RendererStats stats;
    renderer_get_stats(renderer, &stats);
    TEST_ASSERT_EQUAL(0, stats.draw_calls);
    TEST_ASSERT_EQUAL(2, stats.frame_count);

    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    renderer_destroy(renderer);
}

static void test_wireframe_toggle(void) {
    Renderer *renderer = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, renderer_create(g_context, nullptr, &renderer));

    TEST_ASSERT_FALSE(renderer_get_wireframe(renderer));
    renderer_set_wireframe(renderer, true);
    TEST_ASSERT_TRUE(renderer_get_wireframe(renderer));
    renderer_set_wireframe(renderer, false);
    TEST_ASSERT_FALSE(renderer_get_wireframe(renderer));

    renderer_destroy(renderer);
}

static void test_submit_beyond_capacity_fails(void) {
    RendererDesc desc = renderer_desc_default();
    desc.max_commands_per_frame = 1;

    Renderer *renderer = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, renderer_create(g_context, &desc, &renderer));

    const RenderCommand command = {.topology = PRIMITIVE_TOPOLOGY_POINTS, .vertex_count = 1};
    renderer_begin_frame(renderer);
    TEST_ASSERT_EQUAL(RESULT_OK, renderer_submit(renderer, &command));
    TEST_ASSERT_EQUAL(RESULT_ERROR_CAPACITY_EXCEEDED, renderer_submit(renderer, &command));

    renderer_destroy(renderer);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_frame_with_one_draw_updates_stats);
    RUN_TEST(test_stats_reset_each_frame);
    RUN_TEST(test_wireframe_toggle);
    RUN_TEST(test_submit_beyond_capacity_fails);

    return UNITY_END();
}
