/*
 * Runs against SDL3's "offscreen" video driver (see CMakeLists.txt), a
 * real EGL/NVIDIA OpenGL 4.6 context in this environment - ImGui here
 * really initializes and draws, not mocked.
 */
#include "eisenfront/editor.h"

#include "unity.h"

static Window          *g_window;
static GraphicsContext *g_context;

void setUp(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const WindowDesc window_desc = {
        .title = "editor-test",
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

static void test_init_shutdown(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, editor_init(g_window, g_context));
    editor_shutdown();
}

static void test_double_init_fails(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, editor_init(g_window, g_context));
    TEST_ASSERT_EQUAL(RESULT_ERROR_ALREADY_INITIALIZED, editor_init(g_window, g_context));
    editor_shutdown();
}

static void test_shutdown_without_init_is_safe(void) {
    editor_shutdown(); /* no-op, not a crash */
}

static void test_invalid_arguments_rejected(void) {
    TEST_ASSERT_EQUAL(RESULT_ERROR_INVALID_ARGUMENT, editor_init(nullptr, g_context));
    TEST_ASSERT_EQUAL(RESULT_ERROR_INVALID_ARGUMENT, editor_init(g_window, nullptr));
}

static void test_frame_cycle_and_widget_calls(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, editor_init(g_window, g_context));

    editor_new_frame();
    igBegin("Test Window", nullptr, 0);
    igText("hello from a real ig* call");
    bool clicked = igButton("Click me", (ImVec2){0.0f, 0.0f});
    (void)clicked;
    igEnd();
    editor_render();

    const ImDrawData *draw_data = igGetDrawData();
    TEST_ASSERT_NOT_NULL(draw_data);
    TEST_ASSERT_TRUE(draw_data->Valid);
    TEST_ASSERT_TRUE(draw_data->CmdListsCount >= 1);

    editor_shutdown();
}

static void test_capture_flags_available_after_init(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, editor_init(g_window, g_context));
    editor_new_frame();
    /* No assertion on the actual value (no real input device drives
     * this offscreen test) - just confirms the calls are wired to a
     * live ImGuiIO without crashing. */
    (void)editor_wants_capture_mouse();
    (void)editor_wants_capture_keyboard();
    igEndFrame();
    editor_shutdown();
}

static void test_capture_flags_false_before_init(void) {
    TEST_ASSERT_FALSE(editor_wants_capture_mouse());
    TEST_ASSERT_FALSE(editor_wants_capture_keyboard());
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_init_shutdown);
    RUN_TEST(test_double_init_fails);
    RUN_TEST(test_shutdown_without_init_is_safe);
    RUN_TEST(test_invalid_arguments_rejected);
    RUN_TEST(test_frame_cycle_and_widget_calls);
    RUN_TEST(test_capture_flags_available_after_init);
    RUN_TEST(test_capture_flags_false_before_init);

    return UNITY_END();
}
