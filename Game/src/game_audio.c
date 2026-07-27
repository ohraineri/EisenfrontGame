#include "game_audio.h"

#include <stdio.h>

#define GAME_AUDIO_STRIDE_LENGTH_UNITS 1.8f

Result game_audio_create(AudioEngine *engine, const char *assets_dir, GameAudio *out_audio) {
    if (engine == nullptr || assets_dir == nullptr || out_audio == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    *out_audio = (GameAudio){0};

    char wind_path[512], footstep_path[512], ambience_path[512];
    snprintf(wind_path, sizeof(wind_path), "%s/audio/wind_loop.wav", assets_dir);
    snprintf(footstep_path, sizeof(footstep_path), "%s/audio/footstep.wav", assets_dir);
    snprintf(ambience_path, sizeof(ambience_path), "%s/audio/ambience.wav", assets_dir);

    /* Not .stream = true: both clips are short enough (6s / 24s mono)
     * that fully decoding upfront costs nothing worth streaming for,
     * and ma_sound_init_copy() - which sound_play() uses to create each
     * playable instance - failed outright on a streaming source in
     * this sandbox's deviceless fallback engine (see audio.c's file
     * header comment on why no playback device means a deviceless
     * engine rather than a hard failure). Decoding upfront sidesteps
     * that entirely and is the more honest choice for content this
     * short regardless. */
    const SoundDesc wind_desc = {.path = wind_path, .stream = false};
    Result           result = sound_load(engine, &wind_desc, &out_audio->wind_loop);
    if (result != RESULT_OK) {
        return result;
    }

    const SoundDesc footstep_desc = {.path = footstep_path, .stream = false};
    result = sound_load(engine, &footstep_desc, &out_audio->footstep);
    if (result != RESULT_OK) {
        sound_release(engine, out_audio->wind_loop);
        return result;
    }

    const SoundDesc ambience_desc = {.path = ambience_path, .stream = false};
    result = sound_load(engine, &ambience_desc, &out_audio->ambience);
    if (result != RESULT_OK) {
        sound_release(engine, out_audio->footstep);
        sound_release(engine, out_audio->wind_loop);
        return result;
    }

    SoundPlayDesc wind_play = sound_play_desc_default();
    wind_play.loop = true;
    wind_play.spatial = false;
    wind_play.volume = 0.8f;
    sound_play(engine, out_audio->wind_loop, &wind_play, &out_audio->wind_instance);

    SoundPlayDesc ambience_play = sound_play_desc_default();
    ambience_play.loop = true;
    ambience_play.spatial = false;
    ambience_play.volume = 0.6f;
    sound_play(engine, out_audio->ambience, &ambience_play, &out_audio->ambience_instance);

    return RESULT_OK;
}

void game_audio_destroy(AudioEngine *engine, GameAudio *audio) {
    if (engine == nullptr || audio == nullptr) {
        return;
    }
    sound_instance_stop(engine, audio->wind_instance);
    sound_instance_stop(engine, audio->ambience_instance);
    sound_release(engine, audio->ambience);
    sound_release(engine, audio->footstep);
    sound_release(engine, audio->wind_loop);
}

void game_audio_update(AudioEngine *engine, GameAudio *audio, vec3 player_position,
                        float horizontal_distance_moved) {
    audio->distance_since_last_footstep += horizontal_distance_moved;
    if (audio->distance_since_last_footstep < GAME_AUDIO_STRIDE_LENGTH_UNITS) {
        return;
    }
    audio->distance_since_last_footstep -= GAME_AUDIO_STRIDE_LENGTH_UNITS;

    SoundPlayDesc footstep_play = sound_play_desc_default();
    footstep_play.spatial = true;
    footstep_play.position[0] = player_position[0];
    footstep_play.position[1] = player_position[1];
    footstep_play.position[2] = player_position[2];
    footstep_play.volume = 0.7f;
    SoundInstanceId instance;
    sound_play(engine, audio->footstep, &footstep_play, &instance);
}
