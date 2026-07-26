#include "eisenfront/platform.h"

#include "unity.h"

#include <stdio.h>
#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

/* ---- Time ------------------------------------------------------------ */

static void test_time_is_monotonic(void) {
    const TimeNs t0 = time_now_ns();
    sleep_ms(5);
    const TimeNs t1 = time_now_ns();
    TEST_ASSERT_TRUE(t1 > t0);
}

static void test_time_conversion_round_trips(void) {
    const double seconds = 2.5;
    const TimeNs ns = time_seconds_to_ns(seconds);
    TEST_ASSERT_DOUBLE_WITHIN(0.0001, seconds, time_ns_to_seconds(ns));
}

/* ---- Sleep ------------------------------------------------------------ */

static void test_sleep_ms_waits_at_least_requested(void) {
    const TimeNs t0 = time_now_ns();
    sleep_ms(10);
    const double elapsed = time_ns_to_seconds(time_now_ns() - t0);
    TEST_ASSERT_TRUE(elapsed >= 0.009);
}

/* ---- Mutex ------------------------------------------------------------ */

static void test_mutex_lock_unlock_try_lock(void) {
    Mutex *mutex = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, mutex_create(&mutex));

    mutex_lock(mutex);
    TEST_ASSERT_FALSE(mutex_try_lock(mutex));
    mutex_unlock(mutex);
    TEST_ASSERT_TRUE(mutex_try_lock(mutex));
    mutex_unlock(mutex);

    mutex_destroy(mutex);
}

/* ---- Thread ------------------------------------------------------------ */

static int thread_body(void *userdata) {
    int *value = userdata;
    *value = 42;
    return 7;
}

static void test_thread_create_join(void) {
    int              value = 0;
    const ThreadDesc desc = {.name = "test-thread", .fn = thread_body, .userdata = &value};

    Thread *thread = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, thread_create(&desc, &thread));

    int exit_code = 0;
    TEST_ASSERT_EQUAL(RESULT_OK, thread_join(thread, &exit_code));
    TEST_ASSERT_EQUAL(7, exit_code);
    TEST_ASSERT_EQUAL(42, value);
}

static int id_capture_body(void *userdata) {
    uint64_t *id = userdata;
    *id = thread_current_id();
    return 0;
}

static void test_thread_current_id_differs_from_main(void) {
    const uint64_t main_id = thread_current_id();

    uint64_t         worker_id = 0;
    const ThreadDesc desc = {.name = "id-thread", .fn = id_capture_body, .userdata = &worker_id};
    Thread           *thread = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, thread_create(&desc, &thread));
    TEST_ASSERT_EQUAL(RESULT_OK, thread_join(thread, nullptr));

    TEST_ASSERT_NOT_EQUAL(main_id, worker_id);
}

/* ---- Dynamic libraries -------------------------------------------------- */

static void test_dynlib_open_missing_returns_not_found(void) {
    DynLib *lib = nullptr;
    TEST_ASSERT_EQUAL(RESULT_ERROR_NOT_FOUND, dynlib_open("no_such_library.invalid", &lib));
}

/* ---- Filesystem ---------------------------------------------------------- */

static void test_file_write_read_stat_delete_roundtrip(void) {
    const char *path = "eisenfront_platform_test_file.tmp";
    const char *content = "hello eisenfront";

    TEST_ASSERT_EQUAL(RESULT_OK, file_write_all(path, content, strlen(content)));
    TEST_ASSERT_TRUE(file_exists(path));

    FileInfo info;
    TEST_ASSERT_EQUAL(RESULT_OK, file_stat(path, &info));
    TEST_ASSERT_EQUAL(FILE_KIND_REGULAR, info.kind);
    TEST_ASSERT_EQUAL(strlen(content), info.size_bytes);

    void  *data = nullptr;
    size_t size = 0;
    TEST_ASSERT_EQUAL(RESULT_OK, file_read_all(path, &data, &size));
    TEST_ASSERT_EQUAL(strlen(content), size);
    TEST_ASSERT_EQUAL_MEMORY(content, data, size);
    file_free_buffer(data);

    TEST_ASSERT_EQUAL(RESULT_OK, file_delete(path));
    TEST_ASSERT_FALSE(file_exists(path));
}

/* ---- Paths --------------------------------------------------------------- */

static void test_path_join(void) {
    char buffer[64];
    TEST_ASSERT_EQUAL(RESULT_OK, path_join(buffer, sizeof(buffer), "a", "b"));

    char expected[8];
    snprintf(expected, sizeof(expected), "a%cb", PATH_SEPARATOR);
    TEST_ASSERT_EQUAL_STRING(expected, buffer);
}

static void test_path_dirname_basename_extension(void) {
    char buffer[64];

    TEST_ASSERT_EQUAL(RESULT_OK, path_dirname("assets/models/hero.gltf", buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("assets/models", buffer);

    TEST_ASSERT_EQUAL(RESULT_OK, path_basename("assets/models/hero.gltf", buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("hero.gltf", buffer);

    TEST_ASSERT_EQUAL(RESULT_OK, path_extension("assets/models/hero.gltf", buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("gltf", buffer);

    TEST_ASSERT_EQUAL(RESULT_OK, path_dirname("hero.gltf", buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING(".", buffer);
}

/* ---- Directories --------------------------------------------------------- */

static void test_directory_create_iterate_remove(void) {
    const char *dir_path = "eisenfront_platform_test_dir";
    const char *file_name = "inner.tmp";

    directory_remove(dir_path); /* best-effort cleanup from a previous failed run */
    TEST_ASSERT_EQUAL(RESULT_OK, directory_create(dir_path));

    char file_path[128];
    TEST_ASSERT_EQUAL(RESULT_OK, path_join(file_path, sizeof(file_path), dir_path, file_name));
    TEST_ASSERT_EQUAL(RESULT_OK, file_write_all(file_path, "x", 1));

    DirectoryIterator *iterator = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, directory_iterator_open(dir_path, &iterator));

    bool           found = false;
    DirectoryEntry entry;
    while (directory_iterator_next(iterator, &entry)) {
        if (strcmp(entry.name, file_name) == 0 && entry.kind == FILE_KIND_REGULAR) {
            found = true;
        }
    }
    directory_iterator_close(iterator);
    TEST_ASSERT_TRUE(found);

    TEST_ASSERT_EQUAL(RESULT_OK, file_delete(file_path));
    TEST_ASSERT_EQUAL(RESULT_OK, directory_remove(dir_path));
}

/* ---- Result ---------------------------------------------------------------- */

static void test_result_to_string_covers_ok_and_unknown(void) {
    TEST_ASSERT_EQUAL_STRING("OK", result_to_string(RESULT_OK));
    TEST_ASSERT_EQUAL_STRING("Invalid result code", result_to_string((Result)999));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_time_is_monotonic);
    RUN_TEST(test_time_conversion_round_trips);
    RUN_TEST(test_sleep_ms_waits_at_least_requested);
    RUN_TEST(test_mutex_lock_unlock_try_lock);
    RUN_TEST(test_thread_create_join);
    RUN_TEST(test_thread_current_id_differs_from_main);
    RUN_TEST(test_dynlib_open_missing_returns_not_found);
    RUN_TEST(test_file_write_read_stat_delete_roundtrip);
    RUN_TEST(test_path_join);
    RUN_TEST(test_path_dirname_basename_extension);
    RUN_TEST(test_directory_create_iterate_remove);
    RUN_TEST(test_result_to_string_covers_ok_and_unknown);

    return UNITY_END();
}
