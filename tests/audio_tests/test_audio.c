/*
 * Audio is exercised with a real miniaudio engine throughout; when this
 * sandbox has no playback device, audio_engine_create() transparently
 * falls back to a deviceless engine (see the file header comment in
 * audio.c), so mixing/instance bookkeeping is still genuinely verified,
 * just without hitting real speakers.
 */
#include "eisenfront/audio.h"

#include "unity.h"

#ifndef AUDIO_TEST_SOUND_DIR
#define AUDIO_TEST_SOUND_DIR "."
#endif

static char tone_path[512];
static char tone_long_path[512];

void setUp(void) {
    snprintf(tone_path, sizeof(tone_path), "%s/tone.wav", AUDIO_TEST_SOUND_DIR);
    snprintf(tone_long_path, sizeof(tone_long_path), "%s/tone_long.wav", AUDIO_TEST_SOUND_DIR);
}

void tearDown(void) {
}

static void test_create_destroy_engine(void) {
    AudioEngine *engine = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, audio_engine_create(nullptr, &engine));
    TEST_ASSERT_NOT_NULL(engine);
    audio_engine_destroy(engine);
}

static void test_load_release_sound(void) {
    AudioEngine *engine = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, audio_engine_create(nullptr, &engine));

    SoundDesc desc = {.path = tone_path, .stream = false};
    SoundId   sound = AUDIO_INVALID_SOUND_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, sound_load(engine, &desc, &sound));
    TEST_ASSERT_TRUE(sound_is_valid(engine, sound));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.5f, sound_get_duration_seconds(engine, sound));

    sound_release(engine, sound);
    TEST_ASSERT_FALSE(sound_is_valid(engine, sound));

    audio_engine_destroy(engine);
}

static void test_load_missing_file_fails(void) {
    AudioEngine *engine = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, audio_engine_create(nullptr, &engine));

    SoundDesc desc = {.path = "does/not/exist.wav", .stream = false};
    SoundId   sound = AUDIO_INVALID_SOUND_ID;
    TEST_ASSERT_EQUAL(RESULT_ERROR_IO, sound_load(engine, &desc, &sound));
    TEST_ASSERT_FALSE(sound_is_valid(engine, sound));

    audio_engine_destroy(engine);
}

static void test_repeated_load_shares_cache_entry(void) {
    AudioEngine *engine = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, audio_engine_create(nullptr, &engine));

    SoundDesc desc = {.path = tone_path, .stream = false};
    SoundId   first = AUDIO_INVALID_SOUND_ID;
    SoundId   second = AUDIO_INVALID_SOUND_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, sound_load(engine, &desc, &first));
    TEST_ASSERT_EQUAL(RESULT_OK, sound_load(engine, &desc, &second));
    TEST_ASSERT_EQUAL(first, second);

    /* Cache entry survives one release (refcount 2 -> 1) but not two. */
    sound_release(engine, first);
    TEST_ASSERT_TRUE(sound_is_valid(engine, second));
    sound_release(engine, second);
    TEST_ASSERT_FALSE(sound_is_valid(engine, second));

    audio_engine_destroy(engine);
}

static void test_sound_capacity_exceeded(void) {
    AudioEngineDesc engine_desc = audio_engine_desc_default();
    engine_desc.max_sounds = 1;
    AudioEngine *engine = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, audio_engine_create(&engine_desc, &engine));

    SoundDesc desc_a = {.path = tone_path, .stream = false};
    SoundDesc desc_b = {.path = tone_long_path, .stream = false};
    SoundId   a = AUDIO_INVALID_SOUND_ID;
    SoundId   b = AUDIO_INVALID_SOUND_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, sound_load(engine, &desc_a, &a));
    TEST_ASSERT_EQUAL(RESULT_ERROR_CAPACITY_EXCEEDED, sound_load(engine, &desc_b, &b));

    audio_engine_destroy(engine);
}

static void test_play_instance_and_stop(void) {
    AudioEngine *engine = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, audio_engine_create(nullptr, &engine));

    SoundDesc desc = {.path = tone_path, .stream = false};
    SoundId   sound = AUDIO_INVALID_SOUND_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, sound_load(engine, &desc, &sound));

    SoundPlayDesc play_desc = sound_play_desc_default();
    play_desc.loop = false;
    SoundInstanceId instance = AUDIO_INVALID_INSTANCE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, sound_play(engine, sound, &play_desc, &instance));
    TEST_ASSERT_NOT_EQUAL(AUDIO_INVALID_INSTANCE_ID, instance);

    /* Deviceless engines never advance playback themselves (nothing
     * pulls PCM frames), so a fresh instance reads as "still playing"
     * immediately after starting regardless of device availability. */
    TEST_ASSERT_TRUE(sound_instance_is_playing(engine, instance));

    sound_instance_stop(engine, instance);
    TEST_ASSERT_FALSE(sound_instance_is_playing(engine, instance));

    sound_release(engine, sound);
    audio_engine_destroy(engine);
}

static void test_play_capacity_exceeded(void) {
    AudioEngineDesc engine_desc = audio_engine_desc_default();
    engine_desc.max_instances = 1;
    AudioEngine *engine = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, audio_engine_create(&engine_desc, &engine));

    SoundDesc desc = {.path = tone_path, .stream = false};
    SoundId   sound = AUDIO_INVALID_SOUND_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, sound_load(engine, &desc, &sound));

    SoundPlayDesc    play_desc = sound_play_desc_default();
    SoundInstanceId first = AUDIO_INVALID_INSTANCE_ID;
    SoundInstanceId second = AUDIO_INVALID_INSTANCE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, sound_play(engine, sound, &play_desc, &first));
    TEST_ASSERT_EQUAL(RESULT_ERROR_CAPACITY_EXCEEDED,
                       sound_play(engine, sound, &play_desc, &second));

    audio_engine_destroy(engine);
}

static void test_stale_instance_id_after_reuse(void) {
    AudioEngineDesc engine_desc = audio_engine_desc_default();
    engine_desc.max_instances = 1;
    AudioEngine *engine = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, audio_engine_create(&engine_desc, &engine));

    SoundDesc desc = {.path = tone_path, .stream = false};
    SoundId   sound = AUDIO_INVALID_SOUND_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, sound_load(engine, &desc, &sound));

    SoundPlayDesc   play_desc = sound_play_desc_default();
    SoundInstanceId first = AUDIO_INVALID_INSTANCE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, sound_play(engine, sound, &play_desc, &first));
    sound_instance_stop(engine, first);

    SoundInstanceId second = AUDIO_INVALID_INSTANCE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, sound_play(engine, sound, &play_desc, &second));
    TEST_ASSERT_NOT_EQUAL(first, second);
    TEST_ASSERT_FALSE(sound_instance_is_playing(engine, first));
    TEST_ASSERT_TRUE(sound_instance_is_playing(engine, second));

    audio_engine_destroy(engine);
}

static void test_volume_pitch_and_position_setters_do_not_crash(void) {
    AudioEngine *engine = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, audio_engine_create(nullptr, &engine));

    SoundDesc desc = {.path = tone_path, .stream = false};
    SoundId   sound = AUDIO_INVALID_SOUND_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, sound_load(engine, &desc, &sound));

    SoundPlayDesc play_desc = sound_play_desc_default();
    play_desc.spatial = true;
    play_desc.position[0] = 1.0f;
    play_desc.position[1] = 2.0f;
    play_desc.position[2] = 3.0f;
    SoundInstanceId instance = AUDIO_INVALID_INSTANCE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, sound_play(engine, sound, &play_desc, &instance));

    sound_instance_set_volume(engine, instance, 0.5f);
    sound_instance_set_pitch(engine, instance, 1.5f);
    vec3 new_position = {4.0f, 5.0f, 6.0f};
    sound_instance_set_position(engine, instance, new_position);

    vec3 listener_forward = {0.0f, 0.0f, -1.0f};
    vec3 listener_up = {0.0f, 1.0f, 0.0f};
    vec3 listener_pos = {0.0f, 0.0f, 0.0f};
    audio_engine_set_listener(engine, listener_pos, listener_forward, listener_up);
    audio_engine_set_master_volume(engine, 0.75f);

    TEST_ASSERT_TRUE(sound_instance_is_playing(engine, instance));

    audio_engine_destroy(engine);
}

static void test_looping_instance_survives_update(void) {
    AudioEngine *engine = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, audio_engine_create(nullptr, &engine));

    SoundDesc desc = {.path = tone_path, .stream = false};
    SoundId   sound = AUDIO_INVALID_SOUND_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, sound_load(engine, &desc, &sound));

    SoundPlayDesc play_desc = sound_play_desc_default();
    play_desc.loop = true;
    SoundInstanceId instance = AUDIO_INVALID_INSTANCE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, sound_play(engine, sound, &play_desc, &instance));

    for (int i = 0; i < 8; ++i) {
        audio_engine_update(engine);
    }
    TEST_ASSERT_TRUE(sound_instance_is_playing(engine, instance));

    audio_engine_destroy(engine);
}

static void test_invalid_handles_are_safe(void) {
    AudioEngine *engine = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, audio_engine_create(nullptr, &engine));

    TEST_ASSERT_FALSE(sound_is_valid(engine, AUDIO_INVALID_SOUND_ID));
    TEST_ASSERT_FALSE(sound_is_valid(engine, (SoundId)9999));
    TEST_ASSERT_FALSE(sound_instance_is_playing(engine, AUDIO_INVALID_INSTANCE_ID));
    TEST_ASSERT_FALSE(sound_instance_is_playing(engine, (SoundInstanceId)9999));
    TEST_ASSERT_EQUAL(0.0f, sound_get_duration_seconds(engine, AUDIO_INVALID_SOUND_ID));

    /* No-ops, not crashes. */
    sound_release(engine, AUDIO_INVALID_SOUND_ID);
    sound_instance_stop(engine, AUDIO_INVALID_INSTANCE_ID);
    sound_instance_set_volume(engine, AUDIO_INVALID_INSTANCE_ID, 1.0f);

    audio_engine_destroy(engine);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_create_destroy_engine);
    RUN_TEST(test_load_release_sound);
    RUN_TEST(test_load_missing_file_fails);
    RUN_TEST(test_repeated_load_shares_cache_entry);
    RUN_TEST(test_sound_capacity_exceeded);
    RUN_TEST(test_play_instance_and_stop);
    RUN_TEST(test_play_capacity_exceeded);
    RUN_TEST(test_stale_instance_id_after_reuse);
    RUN_TEST(test_volume_pitch_and_position_setters_do_not_crash);
    RUN_TEST(test_looping_instance_survives_update);
    RUN_TEST(test_invalid_handles_are_safe);

    return UNITY_END();
}
