/*
 * Runs against SDL3's "offscreen" video driver (see CMakeLists.txt), a
 * real EGL/NVIDIA OpenGL 4.6 context in this environment - real VAOs and
 * buffers, not mocks.
 */
#include "eisenfront/mesh.h"

#include "unity.h"

static Window          *g_window;
static GraphicsContext *g_context;

void setUp(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const WindowDesc window_desc = {
        .title = "mesh-test",
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

static Vertex make_vertex(float x, float y) {
    return (Vertex){
        .position = {x, y, 0.0f},
        .normal = {0.0f, 0.0f, 1.0f},
        .tangent = {1.0f, 0.0f, 0.0f},
        .uv = {0.0f, 0.0f},
        .bone_indices = {0, 0, 0, 0},
        .bone_weights = {1.0f, 0.0f, 0.0f, 0.0f},
    };
}

static void test_create_static_indexed_quad(void) {
    const Vertex   vertices[4] = {make_vertex(-0.5f, -0.5f), make_vertex(0.5f, -0.5f),
                                   make_vertex(0.5f, 0.5f), make_vertex(-0.5f, 0.5f)};
    const uint32_t indices[6] = {0, 1, 2, 2, 3, 0};
    const MeshDesc desc = {
        .vertices = vertices,
        .vertex_count = 4,
        .indices = indices,
        .index_count = 6,
        .dynamic = false,
    };

    Mesh *mesh = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, mesh_create(&desc, &mesh));
    TEST_ASSERT_TRUE(mesh_get_vertex_array_handle(mesh) != 0);
    TEST_ASSERT_EQUAL(4, mesh_get_vertex_count(mesh));
    TEST_ASSERT_EQUAL(6, mesh_get_index_count(mesh));
    TEST_ASSERT_TRUE(mesh_is_indexed(mesh));
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    mesh_destroy(mesh);
}

static void test_create_non_indexed_triangle(void) {
    const Vertex   vertices[3] = {make_vertex(0.0f, 0.5f), make_vertex(-0.5f, -0.5f),
                                   make_vertex(0.5f, -0.5f)};
    const MeshDesc desc = {.vertices = vertices, .vertex_count = 3, .dynamic = false};

    Mesh *mesh = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, mesh_create(&desc, &mesh));
    TEST_ASSERT_EQUAL(3, mesh_get_vertex_count(mesh));
    TEST_ASSERT_EQUAL(0, mesh_get_index_count(mesh));
    TEST_ASSERT_FALSE(mesh_is_indexed(mesh));

    mesh_destroy(mesh);
}

static void test_static_mesh_rejects_update(void) {
    const Vertex   vertices[3] = {make_vertex(0.0f, 0.0f), make_vertex(1.0f, 0.0f),
                                   make_vertex(0.0f, 1.0f)};
    const MeshDesc desc = {.vertices = vertices, .vertex_count = 3, .dynamic = false};

    Mesh *mesh = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, mesh_create(&desc, &mesh));

    const Vertex replacement = make_vertex(9.0f, 9.0f);
    TEST_ASSERT_EQUAL(RESULT_ERROR_INVALID_STATE, mesh_update_vertices(mesh, 0, &replacement, 1));

    mesh_destroy(mesh);
}

static uint32_t query_bound_buffer_for_attribute(uint32_t vao, uint32_t location) {
    GLint buffer_id = 0;
    glBindVertexArray(vao);
    glGetVertexAttribiv((GLuint)location, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &buffer_id);
    glBindVertexArray(0);
    return (uint32_t)buffer_id;
}

static void test_dynamic_mesh_update_is_visible_in_the_gl_buffer(void) {
    const Vertex   vertices[3] = {make_vertex(0.0f, 0.0f), make_vertex(1.0f, 0.0f),
                                   make_vertex(0.0f, 1.0f)};
    const MeshDesc desc = {.vertices = vertices, .vertex_count = 3, .dynamic = true};

    Mesh *mesh = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, mesh_create(&desc, &mesh));

    const Vertex replacement = make_vertex(42.0f, 24.0f);
    TEST_ASSERT_EQUAL(RESULT_OK, mesh_update_vertices(mesh, 1, &replacement, 1));

    /* Mesh does not expose its underlying GpuBuffer directly; recover it
     * the way any GL debugger would - by asking the VAO which buffer is
     * bound to a given attribute's array binding - to independently
     * confirm the update landed in the actual GL buffer, not just that
     * the call returned RESULT_OK. */
    const uint32_t vbo = query_bound_buffer_for_attribute(mesh_get_vertex_array_handle(mesh), 0);
    TEST_ASSERT_TRUE(vbo != 0);

    Vertex readback[3];
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glGetBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(readback), readback);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    TEST_ASSERT_EQUAL_FLOAT(42.0f, readback[1].position[0]);
    TEST_ASSERT_EQUAL_FLOAT(24.0f, readback[1].position[1]);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, readback[0].position[0]); /* untouched neighbor */

    mesh_destroy(mesh);
}

static void test_instance_buffer_attaches_without_gl_error(void) {
    const Vertex   vertices[3] = {make_vertex(0.0f, 0.0f), make_vertex(1.0f, 0.0f),
                                   make_vertex(0.0f, 1.0f)};
    const MeshDesc desc = {.vertices = vertices, .vertex_count = 3, .dynamic = false};

    Mesh *mesh = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, mesh_create(&desc, &mesh));

    const float      offsets[4 * 3] = {0, 0, 0, 1, 0, 1, 0, 0, 2, 0, 0, 0};
    const BufferDesc instance_desc = {
        .type = BUFFER_TYPE_VERTEX,
        .usage = BUFFER_USAGE_DYNAMIC,
        .initial_data = offsets,
        .size_bytes = sizeof(offsets),
    };
    GpuBuffer *instance_buffer = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, gpu_buffer_create(&instance_desc, &instance_buffer));

    const VertexAttribute instance_attribute = {
        .location = 6,
        .component_count = 3,
        .type = VERTEX_ATTRIB_TYPE_FLOAT,
        .stride_bytes = sizeof(float) * 3,
        .offset_bytes = 0,
        .instance_divisor = 1,
    };
    TEST_ASSERT_EQUAL(RESULT_OK,
                       mesh_set_instance_buffer(mesh, instance_buffer, &instance_attribute, 1));
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    const uint32_t bound = query_bound_buffer_for_attribute(mesh_get_vertex_array_handle(mesh), 6);
    TEST_ASSERT_EQUAL(gpu_buffer_get_gl_handle(instance_buffer), bound);

    gpu_buffer_destroy(instance_buffer);
    mesh_destroy(mesh);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_create_static_indexed_quad);
    RUN_TEST(test_create_non_indexed_triangle);
    RUN_TEST(test_static_mesh_rejects_update);
    RUN_TEST(test_dynamic_mesh_update_is_visible_in_the_gl_buffer);
    RUN_TEST(test_instance_buffer_attaches_without_gl_error);

    return UNITY_END();
}
