/*
 * Runs against SDL3's "offscreen" video driver (see CMakeLists.txt), a
 * real EGL/NVIDIA OpenGL 4.6 context in this environment - textures here
 * are really decoded (via stb_image) and uploaded, not mocked.
 * TEXTURE_TEST_DIR is a CMake compile definition pointing at
 * tests/texture_tests/images (hand-generated minimal BMPs, since this
 * sandbox has neither ImageMagick nor Pillow available to make PNGs).
 */
#include "eisenfront/texture.h"

#include "unity.h"

#include <stdio.h>

#ifndef TEXTURE_TEST_DIR
#    error "TEXTURE_TEST_DIR must be defined by CMake"
#endif

static Window          *g_window;
static GraphicsContext *g_context;

static void path_for(const char *filename, char *out_buffer, size_t capacity) {
    snprintf(out_buffer, capacity, "%s/%s", TEXTURE_TEST_DIR, filename);
}

void setUp(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const WindowDesc window_desc = {
        .title = "texture-test",
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

static void test_load_2d(void) {
    char path[512];
    path_for("red_4x4.bmp", path, sizeof(path));

    Texture *texture = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, texture_load_2d(path, nullptr, &texture));
    TEST_ASSERT_NOT_NULL(texture);
    TEST_ASSERT_TRUE(texture_get_gl_handle(texture) != 0);
    TEST_ASSERT_EQUAL(TEXTURE_TYPE_2D, texture_get_type(texture));
    TEST_ASSERT_EQUAL(4, texture_get_width(texture));
    TEST_ASSERT_EQUAL(4, texture_get_height(texture));
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    texture_release(texture);
}

static void test_duplicate_load_returns_cached_texture(void) {
    char path[512];
    path_for("red_4x4.bmp", path, sizeof(path));

    Texture *first = nullptr;
    Texture *second = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, texture_load_2d(path, nullptr, &first));
    TEST_ASSERT_EQUAL(RESULT_OK, texture_load_2d(path, nullptr, &second));
    TEST_ASSERT_EQUAL_PTR(first, second);

    texture_release(first);
    texture_release(second);
}

static void test_missing_file_fails_to_load(void) {
    char path[512];
    path_for("does_not_exist.bmp", path, sizeof(path));

    Texture *texture = nullptr;
    const Result result = texture_load_2d(path, nullptr, &texture);
    TEST_ASSERT_TRUE(result != RESULT_OK);
}

static void test_bind_produces_no_gl_error(void) {
    char path[512];
    path_for("red_4x4.bmp", path, sizeof(path));

    Texture *texture = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, texture_load_2d(path, nullptr, &texture));
    texture_bind(texture, 0);
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());
    texture_release(texture);
}

static void test_load_2d_array(void) {
    char red_path[512], blue_path[512];
    path_for("red_4x4.bmp", red_path, sizeof(red_path));
    path_for("blue_4x4.bmp", blue_path, sizeof(blue_path));
    const char *paths[2] = {red_path, blue_path};

    Texture *texture = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, texture_load_2d_array(paths, 2, nullptr, &texture));
    TEST_ASSERT_EQUAL(TEXTURE_TYPE_2D_ARRAY, texture_get_type(texture));
    TEST_ASSERT_EQUAL(4, texture_get_width(texture));
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    texture_release(texture);
}

static void test_load_2d_array_rejects_size_mismatch(void) {
    char red_path[512], mismatched_path[512];
    path_for("red_4x4.bmp", red_path, sizeof(red_path));
    path_for("mismatched_2x2.bmp", mismatched_path, sizeof(mismatched_path));
    const char *paths[2] = {red_path, mismatched_path};

    Texture *texture = nullptr;
    TEST_ASSERT_EQUAL(RESULT_ERROR_INVALID_ARGUMENT, texture_load_2d_array(paths, 2, nullptr, &texture));
}

static void test_load_cubemap(void) {
    char faces[6][512];
    const char *names[6] = {"cube_px.bmp", "cube_nx.bmp", "cube_py.bmp",
                             "cube_ny.bmp", "cube_pz.bmp", "cube_nz.bmp"};
    const char *paths[6];
    for (int i = 0; i < 6; ++i) {
        path_for(names[i], faces[i], sizeof(faces[i]));
        paths[i] = faces[i];
    }

    Texture *texture = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, texture_load_cubemap(paths, nullptr, &texture));
    TEST_ASSERT_EQUAL(TEXTURE_TYPE_CUBEMAP, texture_get_type(texture));
    TEST_ASSERT_EQUAL(4, texture_get_width(texture));
    TEST_ASSERT_EQUAL(4, texture_get_height(texture));
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    texture_release(texture);
}

static void test_create_from_pixels_roundtrips(void) {
    unsigned char pixels[4 * 4 * 4];
    for (int i = 0; i < 16; ++i) {
        pixels[i * 4 + 0] = 10;
        pixels[i * 4 + 1] = 20;
        pixels[i * 4 + 2] = 30;
        pixels[i * 4 + 3] = 255;
    }

    Texture *texture = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       texture_create_from_pixels(4, 4, TEXTURE_FORMAT_RGBA8, pixels, nullptr, &texture));
    TEST_ASSERT_EQUAL(4, texture_get_width(texture));

    unsigned char readback[4 * 4 * 4] = {0};
    glGetTextureSubImage(texture_get_gl_handle(texture), 0, 0, 0, 0, 4, 4, 1, GL_RGBA,
                          GL_UNSIGNED_BYTE, sizeof(readback), readback);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pixels, readback, sizeof(pixels));

    /* create_from_pixels is not cached: two calls never return the same
     * pointer even with identical inputs (there is no path to key on). */
    Texture *second = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       texture_create_from_pixels(4, 4, TEXTURE_FORMAT_RGBA8, pixels, nullptr, &second));
    TEST_ASSERT_NOT_EQUAL(texture, second);

    texture_release(texture);
    texture_release(second);
}

static void test_sampler_bind_unbind_produces_no_gl_error(void) {
    TextureParams params = texture_params_default();
    params.max_anisotropy = 4.0f;

    Sampler *sampler = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, sampler_create(&params, &sampler));

    sampler_bind(sampler, 0);
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());
    sampler_unbind_unit(0);
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    sampler_destroy(sampler);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_load_2d);
    RUN_TEST(test_duplicate_load_returns_cached_texture);
    RUN_TEST(test_missing_file_fails_to_load);
    RUN_TEST(test_bind_produces_no_gl_error);
    RUN_TEST(test_load_2d_array);
    RUN_TEST(test_load_2d_array_rejects_size_mismatch);
    RUN_TEST(test_load_cubemap);
    RUN_TEST(test_create_from_pixels_roundtrips);
    RUN_TEST(test_sampler_bind_unbind_produces_no_gl_error);

    return UNITY_END();
}
