/*
 * Audio - sound loading, mixing, and playback via miniaudio's
 * high-level ma_engine, wrapped behind an instance-owned AudioEngine
 * exactly like PhysicsWorld owns its bodies (see physics.h): one
 * process may own several independent AudioEngines (though one is the
 * normal case), each with its own device, sound cache, and set of live
 * playing instances, addressed through generational SoundId /
 * SoundInstanceId handles rather than raw pointers.
 *
 * Two-tier resource model, matching what miniaudio itself recommends
 * for "play the same clip many times at once": a Sound is the decoded
 * (or streaming) audio data loaded once from a path and cached by path
 * exactly like Shader/Texture (repeated sound_load() calls for the same
 * path return the same cached Sound, refcounted); a SoundInstance is a
 * lightweight playable voice created from a Sound via sound_play() -
 * many instances of one Sound may play concurrently (footsteps,
 * gunshots, ...), each with its own volume/pitch/position/loop state.
 * Non-looping instances free themselves once they finish playing; call
 * audio_engine_update() once per frame so that reclamation actually
 * happens (mixing itself runs on miniaudio's own audio device thread
 * and needs no per-frame pump - update() exists only to reclaim
 * finished one-shot instance slots, the audio equivalent of Asset
 * Manager's GPU-upload pump).
 *
 * Scope, stated plainly rather than left implicit:
 *   - One master bus. No mixing groups/buses/categories (e.g. separate
 *     music/SFX/voice volumes) and no DSP effects or filters beyond
 *     miniaudio's default resampling - a production engine chasing
 *     per-category ducking or a real signal chain would add
 *     ma_sound_group on top of this without changing it.
 *   - Spatialization is miniaudio's built-in distance-attenuated
 *     panning (inverse distance model) driven by a single listener; no
 *     support for multiple simultaneous listeners (split-screen) or
 *     custom attenuation curves.
 *   - Decoding formats are whatever miniaudio itself decodes
 *     out of the box (WAV, MP3, FLAC); no Vorbis/Opus without adding
 *     those decoders to the vendored miniaudio build, which this module
 *     does not do.
 *   - miniaudio manages its own internal device and (for streaming
 *     sounds) decode thread rather than going through Platform's
 *     Thread/Mutex - a deliberate exception to the usual "only Platform
 *     touches OS threading primitives" rule, made for the same reason
 *     Window/Input let SDL3 manage its own internals: the thread is
 *     entirely private to the vendored library and never crosses this
 *     module's API boundary.
 *
 * Dependencies: Core, Platform, cglm (positions/vectors); miniaudio
 * (privately, in audio.c only).
 */
#ifndef EISENFRONT_AUDIO_H
#define EISENFRONT_AUDIO_H

#include "core.h"

#include <cglm/cglm.h>

typedef struct AudioEngineDesc {
    uint32_t max_sounds;         /* cache capacity: distinct loaded paths */
    uint32_t max_instances;      /* concurrent playing voices */
    float    master_volume;      /* 0..1+, 1.0 = unmodified */
} AudioEngineDesc;

ENGINE_API AudioEngineDesc audio_engine_desc_default(void);

typedef struct AudioEngine AudioEngine;

ENGINE_API Result audio_engine_create(const AudioEngineDesc *desc, AudioEngine **out_engine);
ENGINE_API void   audio_engine_destroy(AudioEngine *engine);

/* Reclaims finished non-looping instances; does not drive mixing (that
 * runs on miniaudio's own device thread regardless). Call once per
 * frame, main thread. */
ENGINE_API void audio_engine_update(AudioEngine *engine);

ENGINE_API void audio_engine_set_master_volume(AudioEngine *engine, float volume);

/* Positions the single listener used for spatialized (SoundPlayDesc.spatial
 * == true) instances. forward and up should be orthonormal; both are
 * copied, not retained. */
ENGINE_API void audio_engine_set_listener(AudioEngine *engine, vec3 position, vec3 forward,
                                           vec3 up);

/* ==================================================================== *
 * Sounds - path-keyed, reference-counted decoded/streaming audio data.
 * ==================================================================== */

typedef struct SoundDesc {
    const char *path;
    /* false: fully decoded into memory at load time - lowest playback
     *   latency and jitter, appropriate for short SFX.
     * true: decoded incrementally as it plays, from miniaudio's own
     *   internal decode thread - appropriate for long files (music,
     *   ambience) where full up-front decode would be wasteful. */
    bool        stream;
} SoundDesc;

typedef uint32_t SoundId;
#define AUDIO_INVALID_SOUND_ID ((SoundId)0)

/* Returns an existing cached Sound (refcount incremented) if path was
 * already loaded with the same stream setting; otherwise loads it. */
ENGINE_API Result sound_load(AudioEngine *engine, const SoundDesc *desc, SoundId *out_sound);
ENGINE_API void   sound_release(AudioEngine *engine, SoundId sound);
ENGINE_API bool   sound_is_valid(const AudioEngine *engine, SoundId sound);

/* Total duration in seconds; 0 for a sound with unknown/streaming length. */
ENGINE_API float sound_get_duration_seconds(const AudioEngine *engine, SoundId sound);

/* ==================================================================== *
 * Playback - lightweight instances of a loaded Sound.
 * ==================================================================== */

typedef struct SoundPlayDesc {
    float volume; /* 1.0 = unmodified */
    float pitch;  /* 1.0 = unmodified; also affects playback speed, like miniaudio itself */
    bool  loop;
    bool  spatial;  /* false: always full volume, ignores listener/position. true: distance-attenuated from the listener, using position. */
    vec3  position; /* only read when spatial == true */
} SoundPlayDesc;

ENGINE_API SoundPlayDesc sound_play_desc_default(void);

typedef uint32_t SoundInstanceId;
#define AUDIO_INVALID_INSTANCE_ID ((SoundInstanceId)0)

ENGINE_API Result sound_play(AudioEngine *engine, SoundId sound, const SoundPlayDesc *desc,
                              SoundInstanceId *out_instance);

/* Stops and immediately reclaims the instance's slot - unlike letting a
 * non-looping instance run to completion, no need to wait for
 * audio_engine_update() afterward. */
ENGINE_API void sound_instance_stop(AudioEngine *engine, SoundInstanceId instance);

/* False for an instance that has finished (and so is already reclaimed
 * or pending reclamation), was stopped, or never existed. */
ENGINE_API bool sound_instance_is_playing(const AudioEngine *engine, SoundInstanceId instance);

ENGINE_API void sound_instance_set_volume(AudioEngine *engine, SoundInstanceId instance,
                                           float volume);
ENGINE_API void sound_instance_set_pitch(AudioEngine *engine, SoundInstanceId instance,
                                          float pitch);
/* No-op on an instance created with spatial == false. */
ENGINE_API void sound_instance_set_position(AudioEngine *engine, SoundInstanceId instance,
                                             vec3 position);

#endif /* EISENFRONT_AUDIO_H */
