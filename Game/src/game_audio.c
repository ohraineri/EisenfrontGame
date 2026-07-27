#include "game_audio.h"

#include <stdio.h>

/* Indexed by SurfaceType; must stay in that enum's declaration order. */
static const char *const GAME_AUDIO_FOOTSTEP_FILE_NAMES[SURFACE_TYPE_COUNT] = {
    [SURFACE_TYPE_SOIL] = "footstep_soil.wav",       [SURFACE_TYPE_SAND] = "footstep_sand.wav",
    [SURFACE_TYPE_GRAVEL] = "footstep_gravel.wav",   [SURFACE_TYPE_WOOD] = "footstep_wood.wav",
    [SURFACE_TYPE_METAL] = "footstep_metal.wav",     [SURFACE_TYPE_CONCRETE] = "footstep_concrete.wav",
};

static void release_loaded_footsteps(AudioEngine *engine, GameAudio *audio, uint32_t loaded_count) {
    for (uint32_t i = 0; i < loaded_count; ++i) {
        sound_release(engine, audio->footstep_sounds[i]);
    }
}

Result game_audio_create(AudioEngine *engine, const char *assets_dir, GameAudio *out_audio) {
    if (engine == nullptr || assets_dir == nullptr || out_audio == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    *out_audio = (GameAudio){0};

    char wind_path[512], ambience_path[512];
    snprintf(wind_path, sizeof(wind_path), "%s/audio/wind_loop.wav", assets_dir);
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

    for (uint32_t surface = 0; surface < SURFACE_TYPE_COUNT; ++surface) {
        char footstep_path[512];
        snprintf(footstep_path, sizeof(footstep_path), "%s/audio/%s", assets_dir,
                 GAME_AUDIO_FOOTSTEP_FILE_NAMES[surface]);
        const SoundDesc footstep_desc = {.path = footstep_path, .stream = false};
        result = sound_load(engine, &footstep_desc, &out_audio->footstep_sounds[surface]);
        if (result != RESULT_OK) {
            release_loaded_footsteps(engine, out_audio, surface);
            sound_release(engine, out_audio->wind_loop);
            return result;
        }
    }

    const SoundDesc ambience_desc = {.path = ambience_path, .stream = false};
    result = sound_load(engine, &ambience_desc, &out_audio->ambience);
    if (result != RESULT_OK) {
        release_loaded_footsteps(engine, out_audio, SURFACE_TYPE_COUNT);
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
    release_loaded_footsteps(engine, audio, SURFACE_TYPE_COUNT);
    sound_release(engine, audio->wind_loop);
}

void game_audio_play_footstep(AudioEngine *engine, GameAudio *audio, const InfantryFootContactEvent *event) {
    SoundPlayDesc footstep_play = sound_play_desc_default();
    footstep_play.spatial = true;
    footstep_play.position[0] = event->world_position[0];
    footstep_play.position[1] = event->world_position[1];
    footstep_play.position[2] = event->world_position[2];
    /* Simple linear map from event->intensity, not a spec-mandated
     * curve - quieter at Walk/Crouch, louder at Sprint. */
    footstep_play.volume = 0.4f + 0.4f * event->intensity;
    SoundInstanceId instance;
    sound_play(engine, audio->footstep_sounds[event->surface_type], &footstep_play, &instance);
}
