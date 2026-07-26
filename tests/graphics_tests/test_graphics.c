/*
 * Runs against SDL3's "offscreen" video driver (see CMakeLists.txt), which
 * in this environment resolves to a real EGL/NVIDIA OpenGL 4.6 context -
 * this exercises the genuine GL context creation and debug-callback path,
 * not a mock.
 */
#include "eisenfront/graphics_context.h"

#include "unity.h"

static Window *g_window;

void setUp(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const WindowDesc desc = {
        .title = "graphics-test",
        .width = 640,
        .height = 480,
        .mode = WINDOW_MODE_WINDOWED,
        .resizable = false,
        .monitor_index = -1,
    };
    TEST_ASSERT_EQUAL(RESULT_OK, window_create(&desc, &g_window));
}

void tearDown(void) {
    window_destroy(g_window);
    g_window = nullptr;
    window_shutdown();
}

static void test_context_create_reports_at_least_4_6(void) {
    GraphicsContextDesc desc = graphics_context_desc_default();
    desc.vsync = VSYNC_OFF;

    GraphicsContext *context = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, graphics_context_create(g_window, &desc, &context));
    TEST_ASSERT_NOT_NULL(context);

    int32_t major = 0, minor = 0;
    graphics_context_get_version(context, &major, &minor);
    TEST_ASSERT_TRUE(major * 10 + minor >= 46);

    graphics_context_destroy(context);
}

static void test_framebuffer_info_is_sane(void) {
    GraphicsContextDesc desc = graphics_context_desc_default();
    desc.vsync = VSYNC_OFF;

    GraphicsContext *context = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, graphics_context_create(g_window, &desc, &context));

    FramebufferInfo info;
    graphics_context_get_framebuffer_info(context, &info);
    TEST_ASSERT_TRUE(info.red_bits >= 8);
    TEST_ASSERT_TRUE(info.green_bits >= 8);
    TEST_ASSERT_TRUE(info.blue_bits >= 8);
    TEST_ASSERT_TRUE(info.depth_bits >= 16);
    TEST_ASSERT_TRUE(info.double_buffered);

    graphics_context_destroy(context);
}

static void test_vsync_set_get_round_trips(void) {
    GraphicsContextDesc desc = graphics_context_desc_default();
    desc.vsync = VSYNC_OFF;

    GraphicsContext *context = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, graphics_context_create(g_window, &desc, &context));
    TEST_ASSERT_EQUAL(VSYNC_OFF, graphics_context_get_vsync(context));

    TEST_ASSERT_EQUAL(RESULT_OK, graphics_context_set_vsync(context, VSYNC_ON));
    TEST_ASSERT_EQUAL(VSYNC_ON, graphics_context_get_vsync(context));

    graphics_context_destroy(context);
}

static int                   g_debug_calls;
static GraphicsDebugSeverity g_last_severity;

static void on_debug_message(GraphicsDebugSeverity severity, const char *message, void *userdata) {
    (void)message;
    (void)userdata;
    g_debug_calls += 1;
    g_last_severity = severity;
}

static void test_debug_callback_fires_on_gl_error(void) {
    GraphicsContextDesc desc = graphics_context_desc_default();
    desc.debug = true;
    desc.vsync = VSYNC_OFF;

    GraphicsContext *context = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, graphics_context_create(g_window, &desc, &context));

    g_debug_calls = 0;
    graphics_context_set_debug_callback(context, on_debug_message, nullptr);

    /* An invalid enum is a deterministic, portable way to provoke a
     * GL_DEBUG_TYPE_ERROR message; GL_DEBUG_OUTPUT_SYNCHRONOUS means the
     * callback has already run by the time glEnable() returns. */
    glEnable(0xDEAD);

    TEST_ASSERT_TRUE(g_debug_calls > 0);
    TEST_ASSERT_EQUAL(GRAPHICS_DEBUG_SEVERITY_HIGH, g_last_severity);

    graphics_context_destroy(context);
}

static void test_swap_buffers_does_not_crash(void) {
    GraphicsContextDesc desc = graphics_context_desc_default();
    desc.vsync = VSYNC_OFF;

    GraphicsContext *context = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, graphics_context_create(g_window, &desc, &context));

    glClearColor(0.1f, 0.2f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    graphics_context_swap_buffers(context);

    graphics_context_destroy(context);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_context_create_reports_at_least_4_6);
    RUN_TEST(test_framebuffer_info_is_sane);
    RUN_TEST(test_vsync_set_get_round_trips);
    RUN_TEST(test_debug_callback_fires_on_gl_error);
    RUN_TEST(test_swap_buffers_does_not_crash);

    return UNITY_END();
}
