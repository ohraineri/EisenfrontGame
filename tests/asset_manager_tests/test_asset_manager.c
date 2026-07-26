/*
 * Runs against SDL3's "offscreen" video driver (see CMakeLists.txt), a
 * real EGL/NVIDIA OpenGL 4.6 context in this environment. Reuses the
 * Shader and Texture modules' own test fixtures (a real .vert/.frag
 * pair, a real BMP) plus a hand-built minimal glTF triangle (embedded
 * base64 buffer, no external .bin needed) and a small raw byte blob for
 * the audio/font data paths.
 */
#include "eisenfront/asset_manager.h"

#include "unity.h"

#include <stdio.h>
#include <string.h>

#ifndef ASSET_TEST_DATA_DIR
#    error "ASSET_TEST_DATA_DIR must be defined by CMake"
#endif
#ifndef ASSET_TEST_SHADER_DIR
#    error "ASSET_TEST_SHADER_DIR must be defined by CMake"
#endif
#ifndef ASSET_TEST_TEXTURE_DIR
#    error "ASSET_TEST_TEXTURE_DIR must be defined by CMake"
#endif

static Window          *g_window;
static GraphicsContext *g_context;

static void path_in(const char *dir, const char *filename, char *out_buffer, size_t capacity) {
    snprintf(out_buffer, capacity, "%s/%s", dir, filename);
}

void setUp(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const WindowDesc window_desc = {
        .title = "asset-manager-test",
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
    TEST_ASSERT_EQUAL(RESULT_OK, asset_manager_init(2));
}

void tearDown(void) {
    asset_manager_shutdown();
    texture_system_shutdown();
    shader_system_shutdown();
    graphics_context_destroy(g_context);
    g_context = nullptr;
    window_destroy(g_window);
    g_window = nullptr;
    window_shutdown();
}

static void test_sync_texture_load(void) {
    char path[512];
    path_in(ASSET_TEST_TEXTURE_DIR, "red_4x4.bmp", path, sizeof(path));

    AssetHandle *handle = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, asset_load_texture(path, nullptr, &handle));
    TEST_ASSERT_EQUAL(ASSET_STATUS_READY, asset_get_status(handle));
    TEST_ASSERT_EQUAL(ASSET_TYPE_TEXTURE, asset_get_type(handle));

    Texture *texture = asset_get_texture(handle);
    TEST_ASSERT_NOT_NULL(texture);
    TEST_ASSERT_EQUAL(4, texture_get_width(texture));
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    asset_release(handle);
}

static void test_sync_shader_load(void) {
    char vertex_path[512], fragment_path[512];
    path_in(ASSET_TEST_SHADER_DIR, "triangle.vert", vertex_path, sizeof(vertex_path));
    path_in(ASSET_TEST_SHADER_DIR, "triangle.frag", fragment_path, sizeof(fragment_path));
    const ShaderProgramDesc desc = {.vertex_path = vertex_path, .fragment_path = fragment_path};

    AssetHandle *handle = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, asset_load_shader(&desc, &handle));
    TEST_ASSERT_EQUAL(ASSET_STATUS_READY, asset_get_status(handle));

    ShaderProgram *program = asset_get_shader(handle);
    TEST_ASSERT_NOT_NULL(program);
    TEST_ASSERT_TRUE(shader_program_get_gl_handle(program) != 0);

    asset_release(handle);
}

static void test_sync_model_load(void) {
    char path[512];
    path_in(ASSET_TEST_DATA_DIR, "triangle.gltf", path, sizeof(path));

    AssetHandle *handle = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, asset_load_model(path, &handle));
    TEST_ASSERT_EQUAL(ASSET_STATUS_READY, asset_get_status(handle));

    Mesh *const *meshes = nullptr;
    uint32_t     mesh_count = 0;
    TEST_ASSERT_EQUAL(RESULT_OK, asset_get_model_meshes(handle, &meshes, &mesh_count));
    TEST_ASSERT_EQUAL(1, mesh_count);
    TEST_ASSERT_EQUAL(3, mesh_get_vertex_count(meshes[0]));
    TEST_ASSERT_TRUE(mesh_is_indexed(meshes[0]));
    TEST_ASSERT_EQUAL(3, mesh_get_index_count(meshes[0]));
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    asset_release(handle);
}

static void test_broken_model_fails_to_load(void) {
    char path[512];
    path_in(ASSET_TEST_DATA_DIR, "broken.gltf", path, sizeof(path));

    AssetHandle *handle = nullptr;
    TEST_ASSERT_NOT_EQUAL(RESULT_OK, asset_load_model(path, &handle));
}

static void test_duplicate_sync_load_returns_cached_handle(void) {
    char path[512];
    path_in(ASSET_TEST_TEXTURE_DIR, "red_4x4.bmp", path, sizeof(path));

    AssetHandle *first = nullptr;
    AssetHandle *second = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, asset_load_texture(path, nullptr, &first));
    TEST_ASSERT_EQUAL(RESULT_OK, asset_load_texture(path, nullptr, &second));
    TEST_ASSERT_EQUAL_PTR(first, second);

    asset_release(first);
    asset_release(second);
}

static int  g_async_callback_count;
static void on_async_loaded(AssetHandle *handle, void *userdata) {
    (void)handle;
    int *counter = userdata;
    *counter += 1;
}

static void test_async_texture_load_completes_via_update(void) {
    char path[512];
    path_in(ASSET_TEST_TEXTURE_DIR, "green_4x4.bmp", path, sizeof(path));

    g_async_callback_count = 0;
    AssetHandle *handle = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, asset_load_texture_async(path, nullptr, on_async_loaded,
                                                            &g_async_callback_count, &handle));
    TEST_ASSERT_EQUAL(ASSET_STATUS_LOADING, asset_get_status(handle));
    TEST_ASSERT_NULL(asset_get_texture(handle)); /* not ready yet */

    AssetStatus status = ASSET_STATUS_LOADING;
    for (int attempt = 0; attempt < 500 && status == ASSET_STATUS_LOADING; ++attempt) {
        asset_manager_update();
        status = asset_get_status(handle);
        if (status == ASSET_STATUS_LOADING) {
            sleep_ms(2);
        }
    }

    TEST_ASSERT_EQUAL(ASSET_STATUS_READY, status);
    TEST_ASSERT_EQUAL(1, g_async_callback_count);
    Texture *texture = asset_get_texture(handle);
    TEST_ASSERT_NOT_NULL(texture);
    TEST_ASSERT_EQUAL(4, texture_get_width(texture));
    TEST_ASSERT_EQUAL(GL_NO_ERROR, glGetError());

    asset_release(handle);
}

static void test_async_model_load_completes_via_update(void) {
    char path[512];
    path_in(ASSET_TEST_DATA_DIR, "triangle.gltf", path, sizeof(path));

    AssetHandle *handle = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK,
                       asset_load_model_async(path, nullptr, nullptr, &handle));

    AssetStatus status = ASSET_STATUS_LOADING;
    for (int attempt = 0; attempt < 500 && status == ASSET_STATUS_LOADING; ++attempt) {
        asset_manager_update();
        status = asset_get_status(handle);
        if (status == ASSET_STATUS_LOADING) {
            sleep_ms(2);
        }
    }

    TEST_ASSERT_EQUAL(ASSET_STATUS_READY, status);
    Mesh *const *meshes = nullptr;
    uint32_t     mesh_count = 0;
    TEST_ASSERT_EQUAL(RESULT_OK, asset_get_model_meshes(handle, &meshes, &mesh_count));
    TEST_ASSERT_EQUAL(1, mesh_count);

    asset_release(handle);
}

static void test_release_while_still_loading_does_not_crash(void) {
    char path[512];
    path_in(ASSET_TEST_TEXTURE_DIR, "blue_4x4.bmp", path, sizeof(path));

    AssetHandle *handle = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, asset_load_texture_async(path, nullptr, nullptr, nullptr, &handle));
    TEST_ASSERT_EQUAL(ASSET_STATUS_LOADING, asset_get_status(handle));

    /* Release before it has finished decoding: must not crash or
     * use-after-free the still-in-flight worker job. */
    asset_release(handle);

    /* Drain the queue so the deferred-destroy path actually runs before
     * this test process exits. */
    for (int attempt = 0; attempt < 500; ++attempt) {
        asset_manager_update();
        sleep_ms(2);
    }
}

static void test_texture_reload_via_asset_manager(void) {
    char path[512];
    path_in(ASSET_TEST_TEXTURE_DIR, "red_4x4.bmp", path, sizeof(path));

    AssetHandle *handle = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, asset_load_texture(path, nullptr, &handle));
    TEST_ASSERT_EQUAL(RESULT_OK, asset_reload(handle));
    TEST_ASSERT_EQUAL(ASSET_STATUS_READY, asset_get_status(handle));
    TEST_ASSERT_NOT_NULL(asset_get_texture(handle));

    asset_release(handle);
}

static void test_audio_and_font_raw_data(void) {
    char path[512];
    path_in(ASSET_TEST_DATA_DIR, "raw_blob.bin", path, sizeof(path));

    AssetHandle *audio_handle = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, asset_load_audio_data(path, &audio_handle));
    TEST_ASSERT_EQUAL(ASSET_STATUS_READY, asset_get_status(audio_handle));

    const void *data = nullptr;
    size_t      size = 0;
    TEST_ASSERT_EQUAL(RESULT_OK, asset_get_data(audio_handle, &data, &size));
    TEST_ASSERT_EQUAL(strlen("ENGINE-ASSET-TEST-BYTES"), size);
    TEST_ASSERT_EQUAL_MEMORY("ENGINE-ASSET-TEST-BYTES", data, size);

    AssetHandle *font_handle = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, asset_load_font_data(path, &font_handle));
    TEST_ASSERT_EQUAL(ASSET_STATUS_READY, asset_get_status(font_handle));
    /* Same path, different type: must be a distinct cache entry, not
     * the same handle as the audio load above. */
    TEST_ASSERT_NOT_EQUAL(audio_handle, font_handle);

    asset_release(audio_handle);
    asset_release(font_handle);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_sync_texture_load);
    RUN_TEST(test_sync_shader_load);
    RUN_TEST(test_sync_model_load);
    RUN_TEST(test_broken_model_fails_to_load);
    RUN_TEST(test_duplicate_sync_load_returns_cached_handle);
    RUN_TEST(test_async_texture_load_completes_via_update);
    RUN_TEST(test_async_model_load_completes_via_update);
    RUN_TEST(test_release_while_still_loading_does_not_crash);
    RUN_TEST(test_texture_reload_via_asset_manager);
    RUN_TEST(test_audio_and_font_raw_data);

    return UNITY_END();
}
