/*
 * Runs against SDL3's "offscreen" video driver (see CMakeLists.txt), a
 * real EGL/NVIDIA OpenGL 4.6 context in this environment - real
 * framebuffers, shaders and draws, not mocks.
 */
#include "eisenfront/framebuffer.h"
#include "eisenfront/lighting.h"
#include "eisenfront/postprocess.h"
#include "eisenfront/shadow.h"
#include "eisenfront/skybox.h"

#include "unity.h"

#include <stdio.h>

#ifndef RENDERER_FX_TEST_TEXTURE_DIR
#    error "RENDERER_FX_TEST_TEXTURE_DIR must be defined by CMake"
#endif

static Window          *g_window;
static GraphicsContext *g_context;

static void path_in(const char *dir, const char *filename, char *out_buffer, size_t capacity) {
    snprintf(out_buffer, capacity, "%s/%s", dir, filename);
}

void setUp(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const WindowDesc window_desc = {
        .title = "renderer-fx-test",
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

    TEST_ASSERT_EQUAL(RESULT_OK, texture_system_init());
}

void tearDown(void) {
    texture_system_shutdown();
    graphics_context_destroy(g_context);
    g_context = nullptr;
    window_destroy(g_window);
    g_window = nullptr;
    window_shutdown();
}

/* ---- Framebuffer --------------------------------------------------------- */

static void test_framebuffer_create_color_and_depth(void) {
    const FramebufferDesc desc = {
        .width = 64, .height = 64, .sample_count = 1, .has_color = true,
        .color_format = FRAMEBUFFER_COLOR_RGBA8, .has_depth = true,
    };
    Framebuffer *fb = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, framebuffer_create(&desc, &fb));
    TEST_ASSERT_TRUE(framebuffer_get_color_texture(fb) != 0);
    TEST_ASSERT_TRUE(framebuffer_get_depth_texture(fb) != 0);
    TEST_ASSERT_EQUAL(64, framebuffer_get_width(fb));

    framebuffer_bind(fb);
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    framebuffer_destroy(fb);
}

static void test_framebuffer_resolve_msaa_to_single_sample(void) {
    const FramebufferDesc ms_desc = {
        .width = 64, .height = 64, .sample_count = 4, .has_color = true,
        .color_format = FRAMEBUFFER_COLOR_RGBA8, .has_depth = false,
    };
    const FramebufferDesc resolved_desc = {
        .width = 64, .height = 64, .sample_count = 1, .has_color = true,
        .color_format = FRAMEBUFFER_COLOR_RGBA8, .has_depth = false,
    };
    Framebuffer *ms_fb = nullptr;
    Framebuffer *resolved_fb = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, framebuffer_create(&ms_desc, &ms_fb));
    TEST_ASSERT_EQUAL(RESULT_OK, framebuffer_create(&resolved_desc, &resolved_fb));

    framebuffer_bind(ms_fb);
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    TEST_ASSERT_EQUAL(RESULT_OK, framebuffer_resolve_to(ms_fb, resolved_fb));
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    unsigned char pixel[4] = {0};
    glGetTextureSubImage(framebuffer_get_color_texture(resolved_fb), 0, 0, 0, 0, 1, 1, 1, GL_RGBA,
                          GL_UNSIGNED_BYTE, sizeof(pixel), pixel);
    TEST_ASSERT_UINT8_WITHIN(2, 64, pixel[0]);  /* 0.25 * 255 */
    TEST_ASSERT_UINT8_WITHIN(2, 128, pixel[1]); /* 0.5 * 255 */

    framebuffer_destroy(resolved_fb);
    framebuffer_destroy(ms_fb);
}

/* ---- Lighting --------------------------------------------------------- */

static void test_lighting_uploads_directional_and_point_lights(void) {
    LightingEnvironment *environment = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, lighting_environment_create(&environment));

    const DirectionalLight sun = {
        .direction = {0.0f, -1.0f, 0.0f}, .color = {1.0f, 1.0f, 1.0f}, .intensity = 3.0f};
    lighting_set_directional(environment, &sun);

    const PointLight point = {
        .position = {1.0f, 2.0f, 3.0f}, .color = {1.0f, 0.0f, 0.0f}, .intensity = 5.0f, .radius = 10.0f};
    TEST_ASSERT_EQUAL(RESULT_OK, lighting_set_point_light(environment, 0, &point));
    lighting_set_point_light_count(environment, 1);

    lighting_upload(environment);
    lighting_bind(environment);

    GLint ubo = 0;
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, LIGHTING_UBO_BINDING, &ubo);
    TEST_ASSERT_TRUE(ubo != 0);

    /* Layout (see lighting.c): directional{direction[4] @0, color+intensity[4] @16}
     * = 32 bytes, then 3 int32 counts + pad = 16 bytes, then point
     * lights start at byte 48. Intensity is color[3], i.e. byte 16+12=28. */
    float directional_intensity = 0.0f;
    glBindBuffer(GL_UNIFORM_BUFFER, (GLuint)ubo);
    glGetBufferSubData(GL_UNIFORM_BUFFER, 28, sizeof(float), &directional_intensity);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, directional_intensity);

    int32_t point_count = 0;
    glGetBufferSubData(GL_UNIFORM_BUFFER, 36, sizeof(int32_t), &point_count);
    TEST_ASSERT_EQUAL(1, point_count);

    float point_position_y = 0.0f;
    glGetBufferSubData(GL_UNIFORM_BUFFER, 48 + 4, sizeof(float), &point_position_y);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, point_position_y);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    lighting_environment_destroy(environment);
}

/* ---- Skybox --------------------------------------------------------- */

static void test_skybox_renders_without_gl_error(void) {
    char faces[6][512];
    const char *names[6] = {"cube_px.bmp", "cube_nx.bmp", "cube_py.bmp",
                             "cube_ny.bmp", "cube_pz.bmp", "cube_nz.bmp"};
    const char *paths[6];
    for (int i = 0; i < 6; ++i) {
        path_in(RENDERER_FX_TEST_TEXTURE_DIR, names[i], faces[i], sizeof(faces[i]));
        paths[i] = faces[i];
    }
    Texture *cubemap = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, texture_load_cubemap(paths, nullptr, &cubemap));

    Skybox *skybox = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, skybox_create(cubemap, &skybox));

    mat4 view, projection;
    glm_lookat((vec3){0, 0, 0}, (vec3){1, 0, 0}, (vec3){0, 1, 0}, view);
    glm_perspective(glm_rad(60.0f), 4.0f / 3.0f, 0.1f, 100.0f, projection);

    skybox_render(skybox, view, projection);
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    skybox_destroy(skybox);
    texture_release(cubemap);
}

/* ---- Shadow --------------------------------------------------------- */

static void test_shadow_map_depth_pass_writes_depth(void) {
    ShadowMap *shadow_map = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, shadow_map_create(256, &shadow_map));

    vec3 light_direction = {0.0f, -1.0f, -0.5f};
    vec3 scene_center = {0.0f, 0.0f, 0.0f};
    shadow_map_set_light(shadow_map, light_direction, scene_center, 10.0f);

    /* A simple triangle to cast into the depth buffer. */
    const float          triangle[9] = {-1, -1, 0, 1, -1, 0, 0, 1, 0};
    const BufferDesc      buffer_desc = {
        .type = BUFFER_TYPE_VERTEX, .usage = BUFFER_USAGE_STATIC, .initial_data = triangle,
        .size_bytes = sizeof(triangle)};
    GpuBuffer *vbo = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, gpu_buffer_create(&buffer_desc, &vbo));
    const VertexAttribute attribute = {
        .location = 0, .component_count = 3, .type = VERTEX_ATTRIB_TYPE_FLOAT,
        .stride_bytes = sizeof(float) * 3, .offset_bytes = 0};
    const VertexArrayDesc array_desc = {.vertex_buffer = vbo, .attributes = &attribute, .attribute_count = 1};
    VertexArray *vao = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, vertex_array_create(&array_desc, &vao));

    mat4 model;
    glm_mat4_identity(model);

    shadow_map_begin(shadow_map);
    shadow_map_draw(shadow_map, model, vertex_array_get_gl_handle(vao), 3, 0);
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    /* At least one texel must differ from the 1.0 (far plane) clear
     * value - i.e. the triangle actually wrote depth. */
    float depth_pixels[256 * 256];
    glGetTextureSubImage(shadow_map_get_depth_texture(shadow_map), 0, 0, 0, 0, 256, 256, 1,
                          GL_DEPTH_COMPONENT, GL_FLOAT, sizeof(depth_pixels), depth_pixels);
    bool found_non_far_depth = false;
    for (int i = 0; i < 256 * 256; ++i) {
        if (depth_pixels[i] < 0.999f) {
            found_non_far_depth = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(found_non_far_depth);

    vertex_array_destroy(vao);
    gpu_buffer_destroy(vbo);
    shadow_map_destroy(shadow_map);
}

/* ---- PostProcess --------------------------------------------------------- */

static void test_postprocess_pipeline_runs_without_gl_error(void) {
    PostProcessDesc desc = postprocess_desc_default(128, 96);
    PostProcess     *pp = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, postprocess_create(&desc, &pp));

    postprocess_begin_scene(pp);
    glClearColor(2.0f, 0.1f, 0.1f, 1.0f); /* > 1.0: exercises the HDR / tone-mapping path */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    framebuffer_bind_default(128, 96);
    postprocess_resolve_and_apply(pp, 128, 96);
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    postprocess_destroy(pp);
}

/* Regression test: postprocess_resolve_and_apply()'s bloom ping-pong
 * loop binds its own half-resolution internal framebuffers
 * (bloom_a/bloom_b) and previously left the last one of those bound
 * for the final composite draw too, instead of the framebuffer the
 * caller bound before calling this function - the composite quad
 * silently rendered into a small internal buffer nobody ever read,
 * leaving the caller's real target only partially covered by whatever
 * had been drawn there earlier. Only reproduces with bloom enabled
 * (the no-bloom path never touches those buffers). */
static void test_postprocess_with_bloom_composites_into_callers_framebuffer(void) {
    PostProcessDesc desc = postprocess_desc_default(128, 96);
    desc.enable_bloom = true;
    PostProcess *pp = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, postprocess_create(&desc, &pp));

    postprocess_begin_scene(pp);
    glClearColor(0.5f, 0.5f, 0.5f, 1.0f); /* < bloom_threshold: composite alone must still land this */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    framebuffer_bind_default(128, 96);
    /* Known sentinel before the composite draw, so the pixel check
     * below proves the composite actually overwrote it rather than
     * coincidentally matching leftover content from an earlier test. */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    GLint expected_draw_framebuffer = 0;
    GLint expected_read_framebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &expected_draw_framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &expected_read_framebuffer);

    postprocess_resolve_and_apply(pp, 128, 96);

    /* Both bindings, not just the draw one: framebuffer_bind() (used
     * throughout the internal bloom ping-pong loop) binds via plain
     * GL_FRAMEBUFFER, setting draw AND read together, so a fix that
     * only restored GL_DRAW_FRAMEBUFFER would leave GL_READ_FRAMEBUFFER
     * on a stale half-resolution bloom buffer - the composite draw
     * would still land correctly (this alone wouldn't catch that), but
     * a caller's subsequent glReadPixels() (e.g. a screenshot) would
     * silently read the wrong buffer. That's exactly what the pixel
     * check below verifies directly. */
    GLint actual_draw_framebuffer = 0;
    GLint actual_read_framebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &actual_draw_framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &actual_read_framebuffer);
    TEST_ASSERT_EQUAL(expected_draw_framebuffer, actual_draw_framebuffer);
    TEST_ASSERT_EQUAL(expected_read_framebuffer, actual_read_framebuffer);
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    /* The strongest check: read back an actual composited pixel from
     * the caller's framebuffer and confirm it isn't just whatever was
     * there before (0,0,0,0 - this offscreen surface's clear state) -
     * i.e. the composite draw's output is actually reachable from the
     * framebuffer binding the caller expects to read from. */
    uint8_t pixel[4] = {0, 0, 0, 0};
    glReadPixels(64, 48, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    TEST_ASSERT_TRUE(pixel[0] > 0 || pixel[1] > 0 || pixel[2] > 0);

    postprocess_destroy(pp);
}

static void test_postprocess_without_bloom(void) {
    PostProcessDesc desc = postprocess_desc_default(64, 64);
    desc.enable_bloom = false;
    PostProcess *pp = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, postprocess_create(&desc, &pp));

    postprocess_begin_scene(pp);
    glClearColor(0.3f, 0.3f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    framebuffer_bind_default(64, 64);
    postprocess_resolve_and_apply(pp, 64, 64);
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    postprocess_destroy(pp);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_framebuffer_create_color_and_depth);
    RUN_TEST(test_framebuffer_resolve_msaa_to_single_sample);
    RUN_TEST(test_lighting_uploads_directional_and_point_lights);
    RUN_TEST(test_skybox_renders_without_gl_error);
    RUN_TEST(test_shadow_map_depth_pass_writes_depth);
    RUN_TEST(test_postprocess_pipeline_runs_without_gl_error);
    RUN_TEST(test_postprocess_with_bloom_composites_into_callers_framebuffer);
    RUN_TEST(test_postprocess_without_bloom);

    return UNITY_END();
}
