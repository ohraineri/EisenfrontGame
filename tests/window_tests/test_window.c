/*
 * Runs against SDL3's "offscreen" video driver (see CMakeLists.txt, which
 * sets SDL_VIDEODRIVER=offscreen for this target) so it passes in headless
 * CI with no real display, while still exercising the real SDL3 window
 * code path including SDL_WINDOW_OPENGL, which "dummy" does not support.
 */
#include "eisenfront/window.h"

#include "unity.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_window_init_shutdown(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());
    TEST_ASSERT_EQUAL(RESULT_ERROR_ALREADY_INITIALIZED, window_init());
    window_shutdown();
}

static void test_window_create_destroy(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const WindowDesc desc = {
        .title = "Eisenfront Test Window",
        .width = 800,
        .height = 600,
        .mode = WINDOW_MODE_WINDOWED,
        .resizable = true,
        .monitor_index = -1,
    };

    Window *window = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, window_create(&desc, &window));
    TEST_ASSERT_NOT_NULL(window);
    TEST_ASSERT_NOT_NULL(window_get_native_handle(window));
    TEST_ASSERT_EQUAL(WINDOW_MODE_WINDOWED, window_get_mode(window));
    TEST_ASSERT_FALSE(window_should_close(window));

    int32_t width = 0;
    int32_t height = 0;
    window_get_size(window, &width, &height);
    TEST_ASSERT_EQUAL(800, width);
    TEST_ASSERT_EQUAL(600, height);

    window_destroy(window);
    window_shutdown();
}

static void test_window_set_title(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const WindowDesc desc = {
        .title = "before", .width = 640, .height = 480, .mode = WINDOW_MODE_WINDOWED,
        .resizable = false, .monitor_index = -1,
    };
    Window *window = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, window_create(&desc, &window));

    TEST_ASSERT_EQUAL(RESULT_OK, window_set_title(window, "after"));

    window_destroy(window);
    window_shutdown();
}

static void test_window_request_close(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const WindowDesc desc = {
        .title = "close-test", .width = 640, .height = 480, .mode = WINDOW_MODE_WINDOWED,
        .resizable = false, .monitor_index = -1,
    };
    Window *window = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, window_create(&desc, &window));

    TEST_ASSERT_FALSE(window_should_close(window));
    window_request_close(window);
    TEST_ASSERT_TRUE(window_should_close(window));

    window_destroy(window);
    window_shutdown();
}

static int g_resize_events;

static void on_window_event(const WindowEventData *event, void *userdata) {
    (void)userdata;
    if (event->type == WINDOW_EVENT_RESIZED) {
        g_resize_events += 1;
    }
}

static void test_window_poll_events_does_not_crash(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const WindowDesc desc = {
        .title = "events", .width = 640, .height = 480, .mode = WINDOW_MODE_WINDOWED,
        .resizable = true, .monitor_index = -1,
    };
    Window *window = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, window_create(&desc, &window));

    g_resize_events = 0;
    window_set_event_callback(window, on_window_event, nullptr);

    window_poll_events();
    TEST_ASSERT_EQUAL(RESULT_OK, window_set_size(window, 1024, 768));
    window_poll_events();

    window_destroy(window);
    window_shutdown();
}

static void test_monitor_enumeration(void) {
    TEST_ASSERT_EQUAL(RESULT_OK, window_init());

    const int32_t count = monitor_get_count();
    TEST_ASSERT_TRUE(count >= 1);

    MonitorInfo info;
    TEST_ASSERT_EQUAL(RESULT_OK, monitor_get_info(0, &info));
    TEST_ASSERT_EQUAL(0, info.index);
    TEST_ASSERT_TRUE(info.width > 0);
    TEST_ASSERT_TRUE(info.height > 0);

    TEST_ASSERT_EQUAL(RESULT_ERROR_NOT_FOUND, monitor_get_info(count + 10, &info));

    window_shutdown();
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_window_init_shutdown);
    RUN_TEST(test_window_create_destroy);
    RUN_TEST(test_window_set_title);
    RUN_TEST(test_window_request_close);
    RUN_TEST(test_window_poll_events_does_not_crash);
    RUN_TEST(test_monitor_enumeration);

    return UNITY_END();
}
