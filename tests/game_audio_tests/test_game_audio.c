/*
 * Real miniaudio engine throughout (deviceless fallback when this
 * sandbox has no playback device - see audio.c's file header comment),
 * loading the actual per-surface .wav files checked into Assets/audio,
 * not synthetic stand-ins - a missing/misnamed footstep_<surface>.wav
 * would fail here exactly like it would at game startup.
 */
#include "game_audio.h"

#include "unity.h"

#ifndef GAME_AUDIO_TEST_ASSETS_DIR
#    error "GAME_AUDIO_TEST_ASSETS_DIR must be defined by CMake"
#endif

static AudioEngine *g_engine;

void setUp(void) {
    AudioEngineDesc desc = audio_engine_desc_default();
    TEST_ASSERT_EQUAL(RESULT_OK, audio_engine_create(&desc, &g_engine));
}

void tearDown(void) {
    audio_engine_destroy(g_engine);
    g_engine = nullptr;
}

static void test_create_loads_a_distinct_sound_per_surface(void) {
    GameAudio audio;
    TEST_ASSERT_EQUAL(RESULT_OK, game_audio_create(g_engine, GAME_AUDIO_TEST_ASSETS_DIR, &audio));

    for (uint32_t i = 0; i < SURFACE_TYPE_COUNT; ++i) {
        TEST_ASSERT_NOT_EQUAL(AUDIO_INVALID_SOUND_ID, audio.footstep_sounds[i]);
        for (uint32_t j = i + 1; j < SURFACE_TYPE_COUNT; ++j) {
            TEST_ASSERT_NOT_EQUAL(audio.footstep_sounds[i], audio.footstep_sounds[j]);
        }
    }

    game_audio_destroy(g_engine, &audio);
}

static void test_play_footstep_succeeds_for_every_surface_type(void) {
    GameAudio audio;
    TEST_ASSERT_EQUAL(RESULT_OK, game_audio_create(g_engine, GAME_AUDIO_TEST_ASSETS_DIR, &audio));

    for (uint32_t i = 0; i < SURFACE_TYPE_COUNT; ++i) {
        InfantryFootContactEvent event = {
            .foot = PLAYER_FOOT_LEFT,
            .world_position = {0.0f, 0.0f, 0.0f},
            .surface_type = (SurfaceType)i,
            .gait = {.stance = PLAYER_STANCE_STANDING, .speed_tier = PLAYER_SPEED_TIER_RUN},
            .movement_speed = 3.0f,
            .intensity = 0.7f,
            .sim_tick_index = 1,
            .grounded = true,
        };
        /* Not directly observable whether playback actually started
         * (no engine-side "did this instance start" query beyond
         * instance handles) - this exercises the real per-surface
         * SoundId lookup + sound_play() call path without crashing,
         * which is what matters for a missing/misnamed asset file. */
        game_audio_play_footstep(g_engine, &audio, &event);
    }

    game_audio_destroy(g_engine, &audio);
}

static void test_create_fails_cleanly_when_a_footstep_file_is_missing(void) {
    GameAudio audio;
    TEST_ASSERT_NOT_EQUAL(RESULT_OK, game_audio_create(g_engine, "/nonexistent/assets/dir", &audio));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_create_loads_a_distinct_sound_per_surface);
    RUN_TEST(test_play_footstep_succeeds_for_every_surface_type);
    RUN_TEST(test_create_fails_cleanly_when_a_footstep_file_is_missing);

    return UNITY_END();
}
