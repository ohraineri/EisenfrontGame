/*
 * Ambient wind (looping, non-spatial) and footsteps (one-shot, spatial)
 * - see Tools/generate_audio.py for where the underlying .wav files
 * came from (offline procedural synthesis, the one necessarily
 * file-based exception in this slice; see main.c's file header
 * comment).
 *
 * Footstep CADENCE does not live here (M10) - this module only knows
 * how to play one, given an InfantryFootContactEvent already produced
 * and dispatched by main.c (see infantry_foot_contact_event.h's
 * ownership-model comment: locomotion producer -> bounded queue ->
 * single dispatcher -> zero or more consumers; audio is one consumer,
 * not the dispatcher).
 */
#ifndef OUTPOST_GAME_AUDIO_H
#define OUTPOST_GAME_AUDIO_H

#include "eisenfront/audio.h"

#include "infantry_foot_contact_event.h"
#include "surface_type.h"

typedef struct GameAudio {
    SoundId wind_loop;
    SoundId footstep_sounds[SURFACE_TYPE_COUNT];
    SoundId ambience;

    SoundInstanceId wind_instance;
    SoundInstanceId ambience_instance;
} GameAudio;

/* assets_dir is the same OUTPOST_ASSETS_DIR main.c already uses for
 * shaders. Starts the wind/ambience loops immediately. */
Result game_audio_create(AudioEngine *engine, const char *assets_dir, GameAudio *out_audio);
void   game_audio_destroy(AudioEngine *engine, GameAudio *audio);

/* Plays one spatial footstep sound at event->world_position, sampled
 * from event->surface_type's set, volume scaled by event->intensity.
 * Called once per dispatched event - never polls or times anything
 * itself. */
void game_audio_play_footstep(AudioEngine *engine, GameAudio *audio, const InfantryFootContactEvent *event);

#endif /* OUTPOST_GAME_AUDIO_H */
