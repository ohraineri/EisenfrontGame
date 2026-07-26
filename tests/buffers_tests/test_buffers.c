/*
 * Runs against SDL3's "offscreen" video driver (see CMakeLists.txt), a
 * real EGL/NVIDIA OpenGL 4.6 context in this environment - these are
 * real GL buffer/VAO objects, not mocks.
 */
#include "eisenfront/buffers.h"

#include "unity.h"

#include <string.h>

static Window          *g_window;
static GraphicsContext *g_context;

void setUp(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const WindowDesc window_desc = {
        .title = "buffers-test",
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
}

void tearDown(void) {
    graphics_context_destroy(g_context);
    g_context = nullptr;
    window_destroy(g_window);
    g_window = nullptr;
    window_shutdown();
}

static void test_static_vertex_buffer_roundtrips_via_glGetBufferSubData(void) {
    const float vertices[6] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    const BufferDesc desc = {
        .type = BUFFER_TYPE_VERTEX,
        .usage = BUFFER_USAGE_STATIC,
        .initial_data = vertices,
        .size_bytes = sizeof(vertices),
        .persistent_mapped = false,
    };

    GpuBuffer *buffer = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, gpu_buffer_create(&desc, &buffer));
    TEST_ASSERT_EQUAL(sizeof(vertices), gpu_buffer_get_size(buffer));
    TEST_ASSERT_TRUE(gpu_buffer_get_gl_handle(buffer) != 0);

    float readback[6] = {0};
    glBindBuffer(GL_ARRAY_BUFFER, gpu_buffer_get_gl_handle(buffer));
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(readback), readback);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(vertices, readback, 6);

    gpu_buffer_destroy(buffer);
}

static void test_update_overwrites_a_region(void) {
    const float initial[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    const BufferDesc desc = {
        .type = BUFFER_TYPE_VERTEX,
        .usage = BUFFER_USAGE_DYNAMIC,
        .initial_data = initial,
        .size_bytes = sizeof(initial),
    };
    GpuBuffer *buffer = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, gpu_buffer_create(&desc, &buffer));

    const float replacement[2] = {9.0f, 8.0f};
    TEST_ASSERT_EQUAL(RESULT_OK,
                       gpu_buffer_update(buffer, sizeof(float) * 2, replacement, sizeof(replacement)));

    float readback[4] = {0};
    glBindBuffer(GL_ARRAY_BUFFER, gpu_buffer_get_gl_handle(buffer));
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(readback), readback);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const float expected[4] = {1.0f, 2.0f, 9.0f, 8.0f};
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(expected, readback, 4);

    gpu_buffer_destroy(buffer);
}

static void test_update_out_of_range_is_rejected(void) {
    const BufferDesc desc = {
        .type = BUFFER_TYPE_VERTEX, .usage = BUFFER_USAGE_DYNAMIC, .size_bytes = 16
    };
    GpuBuffer *buffer = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, gpu_buffer_create(&desc, &buffer));

    const float value = 1.0f;
    TEST_ASSERT_EQUAL(RESULT_ERROR_INVALID_ARGUMENT,
                       gpu_buffer_update(buffer, 12, &value, sizeof(float) * 2));

    gpu_buffer_destroy(buffer);
}

static void test_persistent_mapped_buffer_write_is_visible_to_gl(void) {
    const BufferDesc desc = {
        .type = BUFFER_TYPE_VERTEX,
        .size_bytes = sizeof(float) * 4,
        .persistent_mapped = true,
    };
    GpuBuffer *buffer = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, gpu_buffer_create(&desc, &buffer));

    float *mapped = gpu_buffer_get_mapped_pointer(buffer);
    TEST_ASSERT_NOT_NULL(mapped);
    mapped[0] = 10.0f;
    mapped[1] = 20.0f;
    mapped[2] = 30.0f;
    mapped[3] = 40.0f;

    /* GL_MAP_COHERENT_BIT means this write is visible to the GL server
     * without an explicit flush call, which is exactly what this test
     * verifies by reading it back through a completely separate GL
     * entry point. */
    float readback[4] = {0};
    glBindBuffer(GL_ARRAY_BUFFER, gpu_buffer_get_gl_handle(buffer));
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(readback), readback);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    const float expected[4] = {10.0f, 20.0f, 30.0f, 40.0f};
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(expected, readback, 4);

    gpu_buffer_destroy(buffer);
}

static void test_persistent_buffer_rejects_update(void) {
    const BufferDesc desc = {
        .type = BUFFER_TYPE_VERTEX, .size_bytes = 16, .persistent_mapped = true
    };
    GpuBuffer *buffer = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, gpu_buffer_create(&desc, &buffer));

    const float value = 1.0f;
    TEST_ASSERT_EQUAL(RESULT_ERROR_INVALID_STATE, gpu_buffer_update(buffer, 0, &value, sizeof(value)));

    gpu_buffer_destroy(buffer);
}

static void test_non_persistent_buffer_has_no_mapped_pointer(void) {
    const BufferDesc desc = {.type = BUFFER_TYPE_VERTEX, .size_bytes = 16};
    GpuBuffer *buffer = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, gpu_buffer_create(&desc, &buffer));
    TEST_ASSERT_NULL(gpu_buffer_get_mapped_pointer(buffer));
    gpu_buffer_destroy(buffer);
}

static void test_uniform_buffer_binds_to_indexed_point(void) {
    const float data[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    const BufferDesc desc = {
        .type = BUFFER_TYPE_UNIFORM,
        .usage = BUFFER_USAGE_DYNAMIC,
        .initial_data = data,
        .size_bytes = sizeof(data),
    };
    GpuBuffer *buffer = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, gpu_buffer_create(&desc, &buffer));

    TEST_ASSERT_EQUAL(RESULT_OK, gpu_buffer_bind_indexed(buffer, 0));
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    gpu_buffer_destroy(buffer);
}

static void test_vertex_buffer_cannot_bind_indexed(void) {
    const BufferDesc desc = {.type = BUFFER_TYPE_VERTEX, .size_bytes = 16};
    GpuBuffer *buffer = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, gpu_buffer_create(&desc, &buffer));
    TEST_ASSERT_EQUAL(RESULT_ERROR_INVALID_STATE, gpu_buffer_bind_indexed(buffer, 0));
    gpu_buffer_destroy(buffer);
}

static void test_vertex_array_with_index_buffer(void) {
    const float vertices[8] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
    const uint32_t indices[6] = {0, 1, 2, 2, 3, 0};

    const BufferDesc vertex_desc = {
        .type = BUFFER_TYPE_VERTEX,
        .usage = BUFFER_USAGE_STATIC,
        .initial_data = vertices,
        .size_bytes = sizeof(vertices),
    };
    const BufferDesc index_desc = {
        .type = BUFFER_TYPE_INDEX,
        .usage = BUFFER_USAGE_STATIC,
        .initial_data = indices,
        .size_bytes = sizeof(indices),
    };
    GpuBuffer *vbo = nullptr;
    GpuBuffer *ebo = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, gpu_buffer_create(&vertex_desc, &vbo));
    TEST_ASSERT_EQUAL(RESULT_OK, gpu_buffer_create(&index_desc, &ebo));

    const VertexAttribute attribute = {
        .location = 0,
        .component_count = 2,
        .type = VERTEX_ATTRIB_TYPE_FLOAT,
        .normalized = false,
        .stride_bytes = sizeof(float) * 2,
        .offset_bytes = 0,
        .instance_divisor = 0,
    };
    const VertexArrayDesc array_desc = {
        .vertex_buffer = vbo,
        .index_buffer = ebo,
        .attributes = &attribute,
        .attribute_count = 1,
    };
    VertexArray *array = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, vertex_array_create(&array_desc, &array));
    TEST_ASSERT_TRUE(vertex_array_get_gl_handle(array) != 0);

    /* Confirm the EBO binding really is captured as VAO state: bind the
     * VAO and query which buffer is bound to GL_ELEMENT_ARRAY_BUFFER. */
    glBindVertexArray(vertex_array_get_gl_handle(array));
    GLint bound_ebo = 0;
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &bound_ebo);
    glBindVertexArray(0);
    TEST_ASSERT_EQUAL((GLint)gpu_buffer_get_gl_handle(ebo), bound_ebo);

    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    vertex_array_destroy(array);
    gpu_buffer_destroy(vbo);
    gpu_buffer_destroy(ebo);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_static_vertex_buffer_roundtrips_via_glGetBufferSubData);
    RUN_TEST(test_update_overwrites_a_region);
    RUN_TEST(test_update_out_of_range_is_rejected);
    RUN_TEST(test_persistent_mapped_buffer_write_is_visible_to_gl);
    RUN_TEST(test_persistent_buffer_rejects_update);
    RUN_TEST(test_non_persistent_buffer_has_no_mapped_pointer);
    RUN_TEST(test_uniform_buffer_binds_to_indexed_point);
    RUN_TEST(test_vertex_buffer_cannot_bind_indexed);
    RUN_TEST(test_vertex_array_with_index_buffer);

    return UNITY_END();
}
