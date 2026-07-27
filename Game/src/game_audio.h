/*
 * Ambient wind (looping, non-spatial) and footsteps (one-shot,
 * spatial, timed to player movement) - see Tools/generate_audio.py for
 * where the underlying .wav files came from (offline procedural
 * synthesis, the one necessarily file-based exception in this slice;
 * see main.c's file header comment).
 */
#ifndef OUTPOST_GAME_AUDIO_H
#define OUTPOST_GAME_AUDIO_H

#include "eisenfront/audio.h"

typedef struct GameAudio {
    SoundId wind_loop;
    SoundId footstep;
    SoundId ambience;

    SoundInstanceId wind_instance;
    SoundInstanceId ambience_instance;

    float distance_since_last_footstep;
} GameAudio;

/* assets_dir is the same OUTPOST_ASSETS_DIR main.c already uses for
 * shaders. Starts the wind/ambience loops immediately. */
Result game_audio_create(AudioEngine *engine, const char *assets_dir, GameAudio *out_audio);
void   game_audio_destroy(AudioEngine *engine, GameAudio *audio);

/* Call once per frame with the player's current position and how far
 * they moved (horizontally) since the last call - fires a spatial
 * footstep sound roughly every stride length. */
void game_audio_update(AudioEngine *engine, GameAudio *audio, vec3 player_position,
                        float horizontal_distance_moved);

#endif /* OUTPOST_GAME_AUDIO_H */
