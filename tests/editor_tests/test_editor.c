/*
 * Runs against SDL3's "offscreen" video driver (see CMakeLists.txt), a
 * real EGL/NVIDIA OpenGL 4.6 context in this environment - ImGui here
 * really initializes and draws, not mocked.
 */
#include "eisenfront/editor.h"

#include "unity.h"

#include <SDL3/SDL.h>

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
    /* Always call editor_shutdown() here rather than at the end of each
     * test body: a failed TEST_ASSERT unwinds out of the test function
     * immediately (Unity uses longjmp), skipping anything after it - if
     * cleanup lived there instead, one failing assertion would leave
     * the Editor initialized and cascade into "ALREADY_INITIALIZED"
     * failures on every test after it. editor_shutdown() is always
     * safe to call, initialized or not. */
    editor_shutdown();
    graphics_context_destroy(g_context);
    g_context = nullptr;
    window_destroy(g_window);
    g_window = nullptr;
    window_shutdown();
}

static void test_init_shutdown(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, editor_init(g_window, g_context));
}

static void test_double_init_fails(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, editor_init(g_window, g_context));
    TEST_ASSERT_EQUAL(RESULT_ERROR_ALREADY_INITIALIZED, editor_init(g_window, g_context));
}

static void test_shutdown_without_init_is_safe(void) {
    editor_shutdown(); /* no-op, not a crash */
}

static void test_invalid_arguments_rejected(void) {
    TEST_ASSERT_EQUAL(RESULT_ERROR_INVALID_ARGUMENT, editor_init(nullptr, g_context));
    TEST_ASSERT_EQUAL(RESULT_ERROR_INVALID_ARGUMENT, editor_init(g_window, nullptr));
}

static void run_one_frame(const char *label) {
    editor_new_frame();
    igBegin(label, nullptr, 0);
    igText("hello from a real ig* call");
    bool clicked = igButton("Click me", (ImVec2){0.0f, 0.0f});
    (void)clicked;
    igEnd();
    editor_render();
}

static void test_frame_cycle_and_widget_calls(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, editor_init(g_window, g_context));

    /* ImGui's very first frame after a fresh context never produces
     * draw commands - window sizing/font-atlas state settles over that
     * frame rather than within it, a documented upstream quirk rather
     * than anything specific to this wrapper. Render one throwaway
     * frame first so the frame actually being asserted on is not that
     * first one. */
    run_one_frame("Test Window");
    run_one_frame("Test Window");

    const ImDrawData *draw_data = igGetDrawData();
    TEST_ASSERT_NOT_NULL(draw_data);
    TEST_ASSERT_TRUE(draw_data->Valid);
    TEST_ASSERT_TRUE(draw_data->CmdListsCount >= 1);
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
}

static void test_capture_flags_false_before_init(void) {
    TEST_ASSERT_FALSE(editor_wants_capture_mouse());
    TEST_ASSERT_FALSE(editor_wants_capture_keyboard());
}

/* editor_process_raw_event() is exposed specifically so a caller can
 * chain it with another raw-event consumer (Input) into one combined
 * WindowRawEventFn - see editor.h's file header comment. Both calls
 * below must be safe: before editor_init() (no-ops, per the header
 * comment) and after (forwarded to ImGui without crashing). */
static void test_process_raw_event_safe_before_init(void) {
    SDL_Event event = {0};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = (Uint32)event.type;
    editor_process_raw_event(&event, nullptr);
}

static void test_process_raw_event_forwarded_after_init(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, editor_init(g_window, g_context));
    run_one_frame("Test Window");

    SDL_Event event = {0};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = (Uint32)event.type;
    event.motion.x = 10.0f;
    event.motion.y = 20.0f;
    editor_process_raw_event(&event, nullptr);
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
    RUN_TEST(test_process_raw_event_safe_before_init);
    RUN_TEST(test_process_raw_event_forwarded_after_init);

    return UNITY_END();
}
