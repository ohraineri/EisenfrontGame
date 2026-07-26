#include "eisenfront/audio.h"

#include <stdlib.h>
#include <string.h>

#define MA_NO_ENGINE_MAIN_THREAD_TIMING
#include <miniaudio.h>

#define AUDIO_LOG_CATEGORY "audio"
#define AUDIO_PATH_CAPACITY 256u

#define AUDIO_SOUND_INDEX_BITS 16u
#define AUDIO_SOUND_INDEX_MASK ((1u << AUDIO_SOUND_INDEX_BITS) - 1u)
#define AUDIO_MAX_SOUND_GENERATION ((1u << (32u - AUDIO_SOUND_INDEX_BITS)) - 1u)

#define AUDIO_INSTANCE_INDEX_BITS 16u
#define AUDIO_INSTANCE_INDEX_MASK ((1u << AUDIO_INSTANCE_INDEX_BITS) - 1u)
#define AUDIO_MAX_INSTANCE_GENERATION ((1u << (32u - AUDIO_INSTANCE_INDEX_BITS)) - 1u)

typedef struct SoundSlot {
    uint32_t generation;
    bool     active;
    uint32_t refcount;
    bool     stream;
    char     path[AUDIO_PATH_CAPACITY];
    ma_sound source; /* never started; exists only as the ma_sound_init_copy template */
} SoundSlot;

typedef struct InstanceSlot {
    uint32_t generation;
    bool     active;
    bool     loop;
    ma_sound voice;
} InstanceSlot;

struct AudioEngine {
    ma_engine engine;

    SoundSlot *sounds;
    uint32_t   max_sounds;
    uint32_t  *free_sound_indices;
    uint32_t   free_sound_count;

    InstanceSlot *instances;
    uint32_t      max_instances;
    uint32_t     *free_instance_indices;
    uint32_t      free_instance_count;
};

static uint32_t sound_index_of(SoundId id) {
    return id & AUDIO_SOUND_INDEX_MASK;
}
static uint32_t sound_generation_of(SoundId id) {
    return id >> AUDIO_SOUND_INDEX_BITS;
}
static SoundId make_sound_id(uint32_t index, uint32_t generation) {
    return (SoundId)((generation << AUDIO_SOUND_INDEX_BITS) | (index & AUDIO_SOUND_INDEX_MASK));
}

static uint32_t instance_index_of(SoundInstanceId id) {
    return id & AUDIO_INSTANCE_INDEX_MASK;
}
static uint32_t instance_generation_of(SoundInstanceId id) {
    return id >> AUDIO_INSTANCE_INDEX_BITS;
}
static SoundInstanceId make_instance_id(uint32_t index, uint32_t generation) {
    return (SoundInstanceId)((generation << AUDIO_INSTANCE_INDEX_BITS) |
                              (index & AUDIO_INSTANCE_INDEX_MASK));
}

AudioEngineDesc audio_engine_desc_default(void) {
    return (AudioEngineDesc){
        .max_sounds = 256,
        .max_instances = 256,
        .master_volume = 1.0f,
    };
}

Result audio_engine_create(const AudioEngineDesc *desc, AudioEngine **out_engine) {
    if (out_engine == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    const AudioEngineDesc effective = (desc != nullptr) ? *desc : audio_engine_desc_default();
    if (effective.max_sounds == 0 || effective.max_sounds > AUDIO_SOUND_INDEX_MASK ||
        effective.max_instances == 0 || effective.max_instances > AUDIO_INSTANCE_INDEX_MASK) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    AudioEngine *engine = calloc(1, sizeof(*engine));
    if (engine == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    if (ma_engine_init(nullptr, &engine->engine) != MA_SUCCESS) {
        /* No usable playback device (headless CI, a dedicated server
         * build, this sandbox, ...) - fall back to a deviceless engine.
         * Mixing, spatialization math and instance bookkeeping all still
         * work identically; the only difference is nothing is ever sent
         * to real speakers. */
        LOG_WARN(AUDIO_LOG_CATEGORY,
                 "No playback device available; running audio engine without a device");
        ma_engine_config config = ma_engine_config_init();
        config.noDevice = MA_TRUE;
        config.channels = 2;
        config.sampleRate = 48000;
        if (ma_engine_init(&config, &engine->engine) != MA_SUCCESS) {
            LOG_ERROR(AUDIO_LOG_CATEGORY, "Failed to initialize miniaudio engine");
            free(engine);
            return RESULT_ERROR_MODULE_INIT_FAILED;
        }
    }

    engine->sounds = calloc((size_t)effective.max_sounds + 1, sizeof(SoundSlot));
    engine->free_sound_indices = malloc(sizeof(uint32_t) * effective.max_sounds);
    engine->instances = calloc((size_t)effective.max_instances + 1, sizeof(InstanceSlot));
    engine->free_instance_indices = malloc(sizeof(uint32_t) * effective.max_instances);
    if (engine->sounds == nullptr || engine->free_sound_indices == nullptr ||
        engine->instances == nullptr || engine->free_instance_indices == nullptr) {
        free(engine->sounds);
        free(engine->free_sound_indices);
        free(engine->instances);
        free(engine->free_instance_indices);
        ma_engine_uninit(&engine->engine);
        free(engine);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < effective.max_sounds; ++i) {
        engine->free_sound_indices[i] = effective.max_sounds - i;
    }
    engine->free_sound_count = effective.max_sounds;
    engine->max_sounds = effective.max_sounds;

    for (uint32_t i = 0; i < effective.max_instances; ++i) {
        engine->free_instance_indices[i] = effective.max_instances - i;
    }
    engine->free_instance_count = effective.max_instances;
    engine->max_instances = effective.max_instances;

    ma_engine_set_volume(&engine->engine, effective.master_volume);

    *out_engine = engine;
    return RESULT_OK;
}

void audio_engine_destroy(AudioEngine *engine) {
    if (engine == nullptr) {
        return;
    }
    for (uint32_t i = 1; i <= engine->max_instances; ++i) {
        if (engine->instances[i].active) {
            ma_sound_uninit(&engine->instances[i].voice);
        }
    }
    for (uint32_t i = 1; i <= engine->max_sounds; ++i) {
        if (engine->sounds[i].active) {
            ma_sound_uninit(&engine->sounds[i].source);
        }
    }
    free(engine->instances);
    free(engine->free_instance_indices);
    free(engine->sounds);
    free(engine->free_sound_indices);
    ma_engine_uninit(&engine->engine);
    free(engine);
}

static void reclaim_instance(AudioEngine *engine, uint32_t index) {
    InstanceSlot *slot = &engine->instances[index];
    ma_sound_uninit(&slot->voice);
    slot->active = false;
    slot->generation =
        (slot->generation < AUDIO_MAX_INSTANCE_GENERATION) ? slot->generation + 1 : 0;
    engine->free_instance_indices[engine->free_instance_count++] = index;
}

void audio_engine_update(AudioEngine *engine) {
    ASSERT(engine != nullptr);
    if (engine == nullptr) {
        return;
    }
    for (uint32_t i = 1; i <= engine->max_instances; ++i) {
        InstanceSlot *slot = &engine->instances[i];
        if (slot->active && !slot->loop && ma_sound_at_end(&slot->voice)) {
            reclaim_instance(engine, i);
        }
    }
}

void audio_engine_set_master_volume(AudioEngine *engine, float volume) {
    ASSERT(engine != nullptr);
    if (engine == nullptr) {
        return;
    }
    ma_engine_set_volume(&engine->engine, volume);
}

void audio_engine_set_listener(AudioEngine *engine, vec3 position, vec3 forward, vec3 up) {
    ASSERT(engine != nullptr);
    if (engine == nullptr) {
        return;
    }
    ma_engine_listener_set_position(&engine->engine, 0, position[0], position[1], position[2]);
    ma_engine_listener_set_direction(&engine->engine, 0, forward[0], forward[1], forward[2]);
    ma_engine_listener_set_world_up(&engine->engine, 0, up[0], up[1], up[2]);
}

/* ==================================================================== *
 * Sounds
 * ==================================================================== */

static bool find_cached_sound(AudioEngine *engine, const char *path, bool stream,
                               uint32_t *out_index) {
    for (uint32_t i = 1; i <= engine->max_sounds; ++i) {
        SoundSlot *slot = &engine->sounds[i];
        if (slot->active && slot->stream == stream && strcmp(slot->path, path) == 0) {
            *out_index = i;
            return true;
        }
    }
    return false;
}

Result sound_load(AudioEngine *engine, const SoundDesc *desc, SoundId *out_sound) {
    if (engine == nullptr || desc == nullptr || desc->path == nullptr || out_sound == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    uint32_t cached_index = 0;
    if (find_cached_sound(engine, desc->path, desc->stream, &cached_index)) {
        engine->sounds[cached_index].refcount += 1;
        *out_sound = make_sound_id(cached_index, engine->sounds[cached_index].generation);
        return RESULT_OK;
    }

    if (engine->free_sound_count == 0) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }

    const ma_uint32 flags = desc->stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    const uint32_t  index = engine->free_sound_indices[engine->free_sound_count - 1];
    SoundSlot      *slot = &engine->sounds[index];

    if (ma_sound_init_from_file(&engine->engine, desc->path, flags, nullptr, nullptr,
                                 &slot->source) != MA_SUCCESS) {
        LOG_ERROR(AUDIO_LOG_CATEGORY, "Failed to load sound '%s'", desc->path);
        return RESULT_ERROR_IO;
    }
    engine->free_sound_count -= 1;

    slot->active = true;
    slot->refcount = 1;
    slot->stream = desc->stream;
    snprintf(slot->path, sizeof(slot->path), "%s", desc->path);

    *out_sound = make_sound_id(index, slot->generation);
    LOG_INFO(AUDIO_LOG_CATEGORY, "Loaded sound '%s'", desc->path);
    return RESULT_OK;
}

bool sound_is_valid(const AudioEngine *engine, SoundId sound) {
    if (engine == nullptr || sound == AUDIO_INVALID_SOUND_ID) {
        return false;
    }
    const uint32_t index = sound_index_of(sound);
    if (index == 0 || index > engine->max_sounds) {
        return false;
    }
    return engine->sounds[index].active &&
           engine->sounds[index].generation == sound_generation_of(sound);
}

void sound_release(AudioEngine *engine, SoundId sound) {
    if (!sound_is_valid(engine, sound)) {
        return;
    }
    const uint32_t index = sound_index_of(sound);
    SoundSlot     *slot = &engine->sounds[index];
    if (slot->refcount > 1) {
        slot->refcount -= 1;
        return;
    }

    ma_sound_uninit(&slot->source);
    slot->active = false;
    slot->generation =
        (slot->generation < AUDIO_MAX_SOUND_GENERATION) ? slot->generation + 1 : 0;
    engine->free_sound_indices[engine->free_sound_count++] = index;
}

float sound_get_duration_seconds(const AudioEngine *engine, SoundId sound) {
    if (!sound_is_valid(engine, sound)) {
        return 0.0f;
    }
    /* ma_sound_get_length_in_seconds() takes a non-const pSound (it may
     * need to read ahead in the underlying data source), even though
     * this call is logically read-only from the caller's perspective. */
    SoundSlot *slot = &((AudioEngine *)engine)->sounds[sound_index_of(sound)];
    float      length = 0.0f;
    if (ma_sound_get_length_in_seconds(&slot->source, &length) != MA_SUCCESS) {
        return 0.0f;
    }
    return length;
}

/* ==================================================================== *
 * Playback
 * ==================================================================== */

SoundPlayDesc sound_play_desc_default(void) {
    return (SoundPlayDesc){
        .volume = 1.0f,
        .pitch = 1.0f,
        .loop = false,
        .spatial = false,
        .position = {0.0f, 0.0f, 0.0f},
    };
}

Result sound_play(AudioEngine *engine, SoundId sound, const SoundPlayDesc *desc,
                   SoundInstanceId *out_instance) {
    if (!sound_is_valid(engine, sound) || desc == nullptr || out_instance == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (engine->free_instance_count == 0) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }

    SoundSlot     *source_slot = &engine->sounds[sound_index_of(sound)];
    const uint32_t index = engine->free_instance_indices[engine->free_instance_count - 1];
    InstanceSlot  *slot = &engine->instances[index];

    if (ma_sound_init_copy(&engine->engine, &source_slot->source, 0, nullptr, &slot->voice) !=
        MA_SUCCESS) {
        LOG_ERROR(AUDIO_LOG_CATEGORY, "Failed to create playback instance for '%s'",
                  source_slot->path);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    engine->free_instance_count -= 1;

    slot->active = true;
    slot->loop = desc->loop;
    ma_sound_set_volume(&slot->voice, desc->volume);
    ma_sound_set_pitch(&slot->voice, desc->pitch);
    ma_sound_set_looping(&slot->voice, desc->loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_spatialization_enabled(&slot->voice, desc->spatial ? MA_TRUE : MA_FALSE);
    if (desc->spatial) {
        ma_sound_set_position(&slot->voice, desc->position[0], desc->position[1],
                               desc->position[2]);
    }
    ma_sound_start(&slot->voice);

    *out_instance = make_instance_id(index, slot->generation);
    return RESULT_OK;
}

static bool instance_is_valid(const AudioEngine *engine, SoundInstanceId instance) {
    if (engine == nullptr || instance == AUDIO_INVALID_INSTANCE_ID) {
        return false;
    }
    const uint32_t index = instance_index_of(instance);
    if (index == 0 || index > engine->max_instances) {
        return false;
    }
    return engine->instances[index].active &&
           engine->instances[index].generation == instance_generation_of(instance);
}

void sound_instance_stop(AudioEngine *engine, SoundInstanceId instance) {
    if (!instance_is_valid(engine, instance)) {
        return;
    }
    reclaim_instance(engine, instance_index_of(instance));
}

bool sound_instance_is_playing(const AudioEngine *engine, SoundInstanceId instance) {
    if (!instance_is_valid(engine, instance)) {
        return false;
    }
    const InstanceSlot *slot = &engine->instances[instance_index_of(instance)];
    return ma_sound_is_playing(&slot->voice) == MA_TRUE && !ma_sound_at_end(&slot->voice);
}

void sound_instance_set_volume(AudioEngine *engine, SoundInstanceId instance, float volume) {
    if (!instance_is_valid(engine, instance)) {
        return;
    }
    ma_sound_set_volume(&engine->instances[instance_index_of(instance)].voice, volume);
}

void sound_instance_set_pitch(AudioEngine *engine, SoundInstanceId instance, float pitch) {
    if (!instance_is_valid(engine, instance)) {
        return;
    }
    ma_sound_set_pitch(&engine->instances[instance_index_of(instance)].voice, pitch);
}

void sound_instance_set_position(AudioEngine *engine, SoundInstanceId instance, vec3 position) {
    if (!instance_is_valid(engine, instance)) {
        return;
    }
    ma_sound_set_position(&engine->instances[instance_index_of(instance)].voice, position[0],
                           position[1], position[2]);
}
