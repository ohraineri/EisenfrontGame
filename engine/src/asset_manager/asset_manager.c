#include "eisenfront/asset_manager.h"

#include "eisenfront/platform.h"

#include <cgltf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSET_LOG_CATEGORY "asset"
#define ASSET_KEY_CAPACITY 512u
#define ASSET_CACHE_CAPACITY 512u
#define ASSET_QUEUE_CAPACITY 256u
#define ASSET_MAX_WORKERS 8u

/* ==================================================================== *
 * Model (glTF) CPU-side import
 * ==================================================================== */

typedef struct ModelCpuPrimitive {
    Vertex   *vertices;
    uint32_t  vertex_count;
    uint32_t *indices;
    uint32_t  index_count;
} ModelCpuPrimitive;

typedef struct ModelCpuData {
    ModelCpuPrimitive *primitives;
    uint32_t            primitive_count;
} ModelCpuData;

static void free_model_cpu_data(ModelCpuData *data) {
    for (uint32_t i = 0; i < data->primitive_count; ++i) {
        free(data->primitives[i].vertices);
        free(data->primitives[i].indices);
    }
    free(data->primitives);
    data->primitives = nullptr;
    data->primitive_count = 0;
}

static Result decode_model(const char *path, ModelCpuData *out_data) {
    cgltf_options options = {0};
    cgltf_data   *data = nullptr;
    cgltf_result  parse_result = cgltf_parse_file(&options, path, &data);
    if (parse_result != cgltf_result_success) {
        LOG_ERROR(ASSET_LOG_CATEGORY, "Failed to parse glTF '%s' (cgltf error %d)", path,
                  (int)parse_result);
        return RESULT_ERROR_PLATFORM;
    }
    parse_result = cgltf_load_buffers(&options, data, path);
    if (parse_result != cgltf_result_success) {
        LOG_ERROR(ASSET_LOG_CATEGORY, "Failed to load glTF buffers for '%s' (cgltf error %d)",
                  path, (int)parse_result);
        cgltf_free(data);
        return RESULT_ERROR_PLATFORM;
    }

    uint32_t primitive_count = 0;
    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        primitive_count += (uint32_t)data->meshes[mi].primitives_count;
    }
    if (primitive_count == 0) {
        LOG_ERROR(ASSET_LOG_CATEGORY, "glTF '%s' contains no mesh primitives", path);
        cgltf_free(data);
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    ModelCpuPrimitive *primitives = calloc(primitive_count, sizeof(ModelCpuPrimitive));
    if (primitives == nullptr) {
        cgltf_free(data);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    uint32_t prim_index = 0;
    Result   result = RESULT_OK;
    for (cgltf_size mi = 0; mi < data->meshes_count && result == RESULT_OK; ++mi) {
        const cgltf_mesh *mesh = &data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh->primitives_count && result == RESULT_OK; ++pi) {
            const cgltf_primitive *primitive = &mesh->primitives[pi];

            const cgltf_accessor *position_accessor = nullptr;
            const cgltf_accessor *normal_accessor = nullptr;
            const cgltf_accessor *tangent_accessor = nullptr;
            const cgltf_accessor *uv_accessor = nullptr;
            for (cgltf_size ai = 0; ai < primitive->attributes_count; ++ai) {
                const cgltf_attribute *attribute = &primitive->attributes[ai];
                if (attribute->type == cgltf_attribute_type_position) {
                    position_accessor = attribute->data;
                } else if (attribute->type == cgltf_attribute_type_normal) {
                    normal_accessor = attribute->data;
                } else if (attribute->type == cgltf_attribute_type_tangent) {
                    tangent_accessor = attribute->data;
                } else if (attribute->type == cgltf_attribute_type_texcoord &&
                           attribute->index == 0) {
                    uv_accessor = attribute->data;
                }
            }

            if (position_accessor == nullptr) {
                LOG_ERROR(ASSET_LOG_CATEGORY, "glTF '%s' has a primitive with no POSITION attribute",
                          path);
                result = RESULT_ERROR_INVALID_ARGUMENT;
                break;
            }

            const uint32_t vertex_count = (uint32_t)position_accessor->count;
            Vertex        *vertices = calloc(vertex_count, sizeof(Vertex));
            if (vertices == nullptr) {
                result = RESULT_ERROR_OUT_OF_MEMORY;
                break;
            }
            for (uint32_t vi = 0; vi < vertex_count; ++vi) {
                cgltf_accessor_read_float(position_accessor, vi, vertices[vi].position, 3);
                if (normal_accessor != nullptr) {
                    cgltf_accessor_read_float(normal_accessor, vi, vertices[vi].normal, 3);
                }
                if (tangent_accessor != nullptr) {
                    float tangent[4];
                    cgltf_accessor_read_float(tangent_accessor, vi, tangent, 4);
                    vertices[vi].tangent[0] = tangent[0];
                    vertices[vi].tangent[1] = tangent[1];
                    vertices[vi].tangent[2] = tangent[2];
                }
                if (uv_accessor != nullptr) {
                    cgltf_accessor_read_float(uv_accessor, vi, vertices[vi].uv, 2);
                }
                /* No skinning data imported yet: fully weighted to bone
                 * slot 0, which a non-skinned shader simply never reads. */
                vertices[vi].bone_weights[0] = 1.0f;
            }

            uint32_t *indices = nullptr;
            uint32_t  index_count = 0;
            if (primitive->indices != nullptr) {
                index_count = (uint32_t)primitive->indices->count;
                indices = malloc(sizeof(uint32_t) * index_count);
                if (indices == nullptr) {
                    free(vertices);
                    result = RESULT_ERROR_OUT_OF_MEMORY;
                    break;
                }
                for (uint32_t ii = 0; ii < index_count; ++ii) {
                    indices[ii] = (uint32_t)cgltf_accessor_read_index(primitive->indices, ii);
                }
            }

            primitives[prim_index].vertices = vertices;
            primitives[prim_index].vertex_count = vertex_count;
            primitives[prim_index].indices = indices;
            primitives[prim_index].index_count = index_count;
            prim_index += 1;
        }
    }

    cgltf_free(data);

    if (result != RESULT_OK) {
        ModelCpuData partial = {.primitives = primitives, .primitive_count = prim_index};
        free_model_cpu_data(&partial);
        return result;
    }

    out_data->primitives = primitives;
    out_data->primitive_count = primitive_count;
    return RESULT_OK;
}

static Result upload_model(const ModelCpuData *cpu_data, Mesh ***out_meshes,
                            uint32_t *out_mesh_count) {
    Mesh **meshes = malloc(sizeof(Mesh *) * cpu_data->primitive_count);
    if (meshes == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < cpu_data->primitive_count; ++i) {
        const MeshDesc desc = {
            .vertices = cpu_data->primitives[i].vertices,
            .vertex_count = cpu_data->primitives[i].vertex_count,
            .indices = cpu_data->primitives[i].indices,
            .index_count = cpu_data->primitives[i].index_count,
            .dynamic = false,
        };
        const Result result = mesh_create(&desc, &meshes[i]);
        if (result != RESULT_OK) {
            for (uint32_t j = 0; j < i; ++j) {
                mesh_destroy(meshes[j]);
            }
            free(meshes);
            return result;
        }
    }

    *out_meshes = meshes;
    *out_mesh_count = cpu_data->primitive_count;
    return RESULT_OK;
}

/* ==================================================================== *
 * AssetHandle
 * ==================================================================== */

struct AssetHandle {
    AssetType   type;
    AssetStatus status;
    uint32_t    ref_count;
    char        source_path[ASSET_KEY_CAPACITY];
    /* Set once asset_release() drops ref_count to 0 while a worker
     * thread may still be decoding this handle; see asset_release(). */
    bool        pending_destroy;

    Texture       *texture;
    TextureParams  texture_params;
    ShaderProgram *shader;
    Mesh         **model_meshes;
    uint32_t       model_mesh_count;
    void          *raw_data;
    size_t         raw_size;

    /* Transient: written by a worker thread's decode step, consumed and
     * cleared by asset_manager_update()'s finalize step. */
    void  *cpu_data;
    Result decode_result;

    AssetLoadedFn on_loaded;
    void         *on_loaded_userdata;
};

typedef struct AssetCacheEntry {
    AssetType    type;
    char         key[ASSET_KEY_CAPACITY];
    AssetHandle *handle;
} AssetCacheEntry;

static bool             g_initialized = false;
static AssetCacheEntry  g_cache[ASSET_CACHE_CAPACITY];
static uint32_t         g_cache_count = 0;

static AssetHandle *g_pending_queue[ASSET_QUEUE_CAPACITY];
static uint32_t     g_pending_head = 0, g_pending_tail = 0, g_pending_count = 0;
static Mutex        *g_pending_mutex = nullptr;

static AssetHandle *g_ready_queue[ASSET_QUEUE_CAPACITY];
static uint32_t     g_ready_head = 0, g_ready_tail = 0, g_ready_count = 0;
static Mutex        *g_ready_mutex = nullptr;

static Mutex  *g_running_mutex = nullptr;
static bool    g_workers_running = false;
static Thread *g_workers[ASSET_MAX_WORKERS];
static uint32_t g_worker_count = 0;

static AssetCacheEntry *cache_find_entry(AssetType type, const char *key) {
    for (uint32_t i = 0; i < g_cache_count; ++i) {
        if (g_cache[i].type == type && strcmp(g_cache[i].key, key) == 0) {
            return &g_cache[i];
        }
    }
    return nullptr;
}

static AssetHandle *cache_find(AssetType type, const char *key) {
    AssetCacheEntry *entry = cache_find_entry(type, key);
    return entry != nullptr ? entry->handle : nullptr;
}

static void cache_insert(AssetType type, const char *key, AssetHandle *handle) {
    if (g_cache_count < ASSET_CACHE_CAPACITY) {
        g_cache[g_cache_count].type = type;
        snprintf(g_cache[g_cache_count].key, sizeof(g_cache[g_cache_count].key), "%s", key);
        g_cache[g_cache_count].handle = handle;
        g_cache_count += 1;
    } else {
        LOG_WARN(ASSET_LOG_CATEGORY, "Asset cache full (%u); '%s' will reload on next request",
                 ASSET_CACHE_CAPACITY, key);
    }
}

static void cache_remove(AssetHandle *handle) {
    for (uint32_t i = 0; i < g_cache_count; ++i) {
        if (g_cache[i].handle == handle) {
            g_cache[i] = g_cache[g_cache_count - 1];
            g_cache_count -= 1;
            return;
        }
    }
}

/* ==================================================================== *
 * Queues
 * ==================================================================== */

static bool is_running(void) {
    mutex_lock(g_running_mutex);
    const bool running = g_workers_running;
    mutex_unlock(g_running_mutex);
    return running;
}

static void set_running(bool value) {
    mutex_lock(g_running_mutex);
    g_workers_running = value;
    mutex_unlock(g_running_mutex);
}

static void push_ready(AssetHandle *handle) {
    mutex_lock(g_ready_mutex);
    if (g_ready_count < ASSET_QUEUE_CAPACITY) {
        g_ready_queue[g_ready_tail] = handle;
        g_ready_tail = (g_ready_tail + 1) % ASSET_QUEUE_CAPACITY;
        g_ready_count += 1;
    } else {
        LOG_ERROR(ASSET_LOG_CATEGORY, "Ready queue full; '%s' result dropped", handle->source_path);
    }
    mutex_unlock(g_ready_mutex);
}

static AssetHandle *pop_ready(void) {
    mutex_lock(g_ready_mutex);
    AssetHandle *handle = nullptr;
    if (g_ready_count > 0) {
        handle = g_ready_queue[g_ready_head];
        g_ready_head = (g_ready_head + 1) % ASSET_QUEUE_CAPACITY;
        g_ready_count -= 1;
    }
    mutex_unlock(g_ready_mutex);
    return handle;
}

static Result push_pending(AssetHandle *handle) {
    mutex_lock(g_pending_mutex);
    if (g_pending_count >= ASSET_QUEUE_CAPACITY) {
        mutex_unlock(g_pending_mutex);
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }
    g_pending_queue[g_pending_tail] = handle;
    g_pending_tail = (g_pending_tail + 1) % ASSET_QUEUE_CAPACITY;
    g_pending_count += 1;
    mutex_unlock(g_pending_mutex);
    return RESULT_OK;
}

static AssetHandle *pop_pending(void) {
    mutex_lock(g_pending_mutex);
    AssetHandle *handle = nullptr;
    if (g_pending_count > 0) {
        handle = g_pending_queue[g_pending_head];
        g_pending_head = (g_pending_head + 1) % ASSET_QUEUE_CAPACITY;
        g_pending_count -= 1;
    }
    mutex_unlock(g_pending_mutex);
    return handle;
}

static int worker_thread_main(void *userdata) {
    (void)userdata;
    while (is_running()) {
        AssetHandle *handle = pop_pending();
        if (handle == nullptr) {
            sleep_ms(2);
            continue;
        }

        switch (handle->type) {
            case ASSET_TYPE_TEXTURE: {
                TextureCpuImage *image = malloc(sizeof(TextureCpuImage));
                if (image == nullptr) {
                    handle->decode_result = RESULT_ERROR_OUT_OF_MEMORY;
                } else {
                    handle->decode_result = texture_decode_2d(handle->source_path, image);
                    handle->cpu_data = image;
                }
                break;
            }
            case ASSET_TYPE_MODEL: {
                ModelCpuData *model = malloc(sizeof(ModelCpuData));
                if (model == nullptr) {
                    handle->decode_result = RESULT_ERROR_OUT_OF_MEMORY;
                } else {
                    handle->decode_result = decode_model(handle->source_path, model);
                    handle->cpu_data = model;
                }
                break;
            }
            case ASSET_TYPE_AUDIO_DATA:
            case ASSET_TYPE_FONT_DATA:
                handle->decode_result =
                    file_read_all(handle->source_path, &handle->raw_data, &handle->raw_size);
                break;
            case ASSET_TYPE_SHADER:
            default:
                handle->decode_result = RESULT_ERROR_INVALID_ARGUMENT;
                break;
        }

        push_ready(handle);
    }
    return 0;
}

/* ==================================================================== *
 * Lifecycle
 * ==================================================================== */

Result asset_manager_init(uint32_t worker_thread_count) {
    if (g_initialized) {
        return RESULT_ERROR_ALREADY_INITIALIZED;
    }
    if (worker_thread_count == 0 || worker_thread_count > ASSET_MAX_WORKERS) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    Result result = mutex_create(&g_pending_mutex);
    if (result != RESULT_OK) {
        return result;
    }
    result = mutex_create(&g_ready_mutex);
    if (result != RESULT_OK) {
        mutex_destroy(g_pending_mutex);
        return result;
    }
    result = mutex_create(&g_running_mutex);
    if (result != RESULT_OK) {
        mutex_destroy(g_pending_mutex);
        mutex_destroy(g_ready_mutex);
        return result;
    }

    memset(g_cache, 0, sizeof(g_cache));
    g_cache_count = 0;
    g_pending_head = g_pending_tail = g_pending_count = 0;
    g_ready_head = g_ready_tail = g_ready_count = 0;
    g_workers_running = true;
    g_worker_count = 0;

    for (uint32_t i = 0; i < worker_thread_count; ++i) {
        const ThreadDesc desc = {
            .name = "asset-worker", .fn = worker_thread_main, .userdata = nullptr};
        result = thread_create(&desc, &g_workers[i]);
        if (result != RESULT_OK) {
            set_running(false);
            for (uint32_t j = 0; j < i; ++j) {
                thread_join(g_workers[j], nullptr);
            }
            mutex_destroy(g_pending_mutex);
            mutex_destroy(g_ready_mutex);
            mutex_destroy(g_running_mutex);
            return result;
        }
        g_worker_count += 1;
    }

    g_initialized = true;
    return RESULT_OK;
}

static void destroy_handle(AssetHandle *handle) {
    switch (handle->type) {
        case ASSET_TYPE_TEXTURE:
            if (handle->texture != nullptr) {
                texture_release(handle->texture);
            }
            break;
        case ASSET_TYPE_SHADER:
            if (handle->shader != nullptr) {
                shader_program_release(handle->shader);
            }
            break;
        case ASSET_TYPE_MODEL:
            for (uint32_t i = 0; i < handle->model_mesh_count; ++i) {
                mesh_destroy(handle->model_meshes[i]);
            }
            free(handle->model_meshes);
            break;
        case ASSET_TYPE_AUDIO_DATA:
        case ASSET_TYPE_FONT_DATA:
            file_free_buffer(handle->raw_data);
            break;
    }
    free(handle);
}

static void free_pending_cpu_data(AssetHandle *handle) {
    if (handle->cpu_data == nullptr) {
        return;
    }
    if (handle->type == ASSET_TYPE_TEXTURE) {
        texture_free_cpu_image(handle->cpu_data);
        free(handle->cpu_data);
    } else if (handle->type == ASSET_TYPE_MODEL) {
        free_model_cpu_data(handle->cpu_data);
        free(handle->cpu_data);
    }
    handle->cpu_data = nullptr;
}

void asset_manager_shutdown(void) {
    if (!g_initialized) {
        return;
    }

    set_running(false);
    for (uint32_t i = 0; i < g_worker_count; ++i) {
        thread_join(g_workers[i], nullptr);
    }
    g_worker_count = 0;

    /* Drain both queues: anything still pending never got decoded,
     * anything ready never got finalized - either way it just needs
     * freeing, not uploading. */
    AssetHandle *handle;
    while ((handle = pop_pending()) != nullptr) {
        destroy_handle(handle);
    }
    while ((handle = pop_ready()) != nullptr) {
        free_pending_cpu_data(handle);
        destroy_handle(handle);
    }

    if (g_cache_count > 0) {
        LOG_WARN(ASSET_LOG_CATEGORY, "asset_manager_shutdown with %u asset(s) still cached",
                 g_cache_count);
    }
    for (uint32_t i = 0; i < g_cache_count; ++i) {
        destroy_handle(g_cache[i].handle);
    }
    g_cache_count = 0;

    mutex_destroy(g_pending_mutex);
    mutex_destroy(g_ready_mutex);
    mutex_destroy(g_running_mutex);
    g_pending_mutex = nullptr;
    g_ready_mutex = nullptr;
    g_running_mutex = nullptr;

    g_initialized = false;
}

void asset_manager_update(void) {
    ASSERT(g_initialized);
    if (!g_initialized) {
        return;
    }

    for (;;) {
        AssetHandle *handle = pop_ready();
        if (handle == nullptr) {
            break;
        }

        if (handle->pending_destroy) {
            free_pending_cpu_data(handle);
            destroy_handle(handle);
            continue;
        }

        if (handle->decode_result != RESULT_OK) {
            handle->status = ASSET_STATUS_FAILED;
        } else {
            switch (handle->type) {
                case ASSET_TYPE_TEXTURE: {
                    TextureCpuImage *image = handle->cpu_data;
                    const Result     result =
                        texture_upload_2d(image, &handle->texture_params, &handle->texture);
                    texture_free_cpu_image(image);
                    free(image);
                    handle->status = (result == RESULT_OK) ? ASSET_STATUS_READY : ASSET_STATUS_FAILED;
                    break;
                }
                case ASSET_TYPE_MODEL: {
                    ModelCpuData *model = handle->cpu_data;
                    const Result  result =
                        upload_model(model, &handle->model_meshes, &handle->model_mesh_count);
                    free_model_cpu_data(model);
                    free(model);
                    handle->status = (result == RESULT_OK) ? ASSET_STATUS_READY : ASSET_STATUS_FAILED;
                    break;
                }
                case ASSET_TYPE_AUDIO_DATA:
                case ASSET_TYPE_FONT_DATA:
                    handle->status = ASSET_STATUS_READY;
                    break;
                case ASSET_TYPE_SHADER:
                default:
                    handle->status = ASSET_STATUS_FAILED;
                    break;
            }
        }
        handle->cpu_data = nullptr;

        if (handle->on_loaded != nullptr) {
            handle->on_loaded(handle, handle->on_loaded_userdata);
        }
    }
}

/* ==================================================================== *
 * Synchronous loads
 * ==================================================================== */

Result asset_load_texture(const char *path, const TextureParams *params,
                           AssetHandle **out_handle) {
    if (path == nullptr || out_handle == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return RESULT_ERROR_NOT_INITIALIZED;
    }

    AssetHandle *cached = cache_find(ASSET_TYPE_TEXTURE, path);
    if (cached != nullptr) {
        cached->ref_count += 1;
        *out_handle = cached;
        return RESULT_OK;
    }

    Texture *texture = nullptr;
    Result   result = texture_load_2d(path, params, &texture);
    if (result != RESULT_OK) {
        return result;
    }

    AssetHandle *handle = calloc(1, sizeof(*handle));
    if (handle == nullptr) {
        texture_release(texture);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    handle->type = ASSET_TYPE_TEXTURE;
    handle->status = ASSET_STATUS_READY;
    handle->ref_count = 1;
    snprintf(handle->source_path, sizeof(handle->source_path), "%s", path);
    handle->texture = texture;
    handle->texture_params = (params != nullptr) ? *params : texture_params_default();

    cache_insert(ASSET_TYPE_TEXTURE, path, handle);
    *out_handle = handle;
    return RESULT_OK;
}

Result asset_load_shader(const ShaderProgramDesc *desc, AssetHandle **out_handle) {
    if (desc == nullptr || out_handle == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return RESULT_ERROR_NOT_INITIALIZED;
    }

    char key[ASSET_KEY_CAPACITY];
    snprintf(key, sizeof(key), "%s|%s|%s", desc->vertex_path, desc->fragment_path,
             (desc->geometry_path != nullptr) ? desc->geometry_path : "");

    AssetHandle *cached = cache_find(ASSET_TYPE_SHADER, key);
    if (cached != nullptr) {
        cached->ref_count += 1;
        *out_handle = cached;
        return RESULT_OK;
    }

    ShaderProgram *program = nullptr;
    Result         result = shader_program_load(desc, &program);
    if (result != RESULT_OK) {
        return result;
    }

    AssetHandle *handle = calloc(1, sizeof(*handle));
    if (handle == nullptr) {
        shader_program_release(program);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    handle->type = ASSET_TYPE_SHADER;
    handle->status = ASSET_STATUS_READY;
    handle->ref_count = 1;
    snprintf(handle->source_path, sizeof(handle->source_path), "%s", key);
    handle->shader = program;

    cache_insert(ASSET_TYPE_SHADER, key, handle);
    *out_handle = handle;
    return RESULT_OK;
}

Result asset_load_model(const char *path, AssetHandle **out_handle) {
    if (path == nullptr || out_handle == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return RESULT_ERROR_NOT_INITIALIZED;
    }

    AssetHandle *cached = cache_find(ASSET_TYPE_MODEL, path);
    if (cached != nullptr) {
        cached->ref_count += 1;
        *out_handle = cached;
        return RESULT_OK;
    }

    ModelCpuData cpu_data;
    Result       result = decode_model(path, &cpu_data);
    if (result != RESULT_OK) {
        return result;
    }

    Mesh    **meshes = nullptr;
    uint32_t  mesh_count = 0;
    result = upload_model(&cpu_data, &meshes, &mesh_count);
    free_model_cpu_data(&cpu_data);
    if (result != RESULT_OK) {
        return result;
    }

    AssetHandle *handle = calloc(1, sizeof(*handle));
    if (handle == nullptr) {
        for (uint32_t i = 0; i < mesh_count; ++i) {
            mesh_destroy(meshes[i]);
        }
        free(meshes);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    handle->type = ASSET_TYPE_MODEL;
    handle->status = ASSET_STATUS_READY;
    handle->ref_count = 1;
    snprintf(handle->source_path, sizeof(handle->source_path), "%s", path);
    handle->model_meshes = meshes;
    handle->model_mesh_count = mesh_count;

    cache_insert(ASSET_TYPE_MODEL, path, handle);
    *out_handle = handle;
    return RESULT_OK;
}

static Result load_raw_asset(AssetType type, const char *path, AssetHandle **out_handle) {
    if (path == nullptr || out_handle == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return RESULT_ERROR_NOT_INITIALIZED;
    }

    AssetHandle *cached = cache_find(type, path);
    if (cached != nullptr) {
        cached->ref_count += 1;
        *out_handle = cached;
        return RESULT_OK;
    }

    void  *data = nullptr;
    size_t size = 0;
    Result result = file_read_all(path, &data, &size);
    if (result != RESULT_OK) {
        return result;
    }

    AssetHandle *handle = calloc(1, sizeof(*handle));
    if (handle == nullptr) {
        file_free_buffer(data);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    handle->type = type;
    handle->status = ASSET_STATUS_READY;
    handle->ref_count = 1;
    snprintf(handle->source_path, sizeof(handle->source_path), "%s", path);
    handle->raw_data = data;
    handle->raw_size = size;

    cache_insert(type, path, handle);
    *out_handle = handle;
    return RESULT_OK;
}

Result asset_load_audio_data(const char *path, AssetHandle **out_handle) {
    return load_raw_asset(ASSET_TYPE_AUDIO_DATA, path, out_handle);
}

Result asset_load_font_data(const char *path, AssetHandle **out_handle) {
    return load_raw_asset(ASSET_TYPE_FONT_DATA, path, out_handle);
}

/* ==================================================================== *
 * Asynchronous loads
 * ==================================================================== */

Result asset_load_texture_async(const char *path, const TextureParams *params,
                                 AssetLoadedFn on_loaded, void *userdata,
                                 AssetHandle **out_handle) {
    if (path == nullptr || out_handle == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return RESULT_ERROR_NOT_INITIALIZED;
    }

    AssetHandle *cached = cache_find(ASSET_TYPE_TEXTURE, path);
    if (cached != nullptr) {
        cached->ref_count += 1;
        *out_handle = cached;
        return RESULT_OK;
    }

    AssetHandle *handle = calloc(1, sizeof(*handle));
    if (handle == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    handle->type = ASSET_TYPE_TEXTURE;
    handle->status = ASSET_STATUS_LOADING;
    handle->ref_count = 1;
    snprintf(handle->source_path, sizeof(handle->source_path), "%s", path);
    handle->texture_params = (params != nullptr) ? *params : texture_params_default();
    handle->on_loaded = on_loaded;
    handle->on_loaded_userdata = userdata;

    cache_insert(ASSET_TYPE_TEXTURE, path, handle);
    const Result result = push_pending(handle);
    if (result != RESULT_OK) {
        cache_remove(handle);
        free(handle);
        return result;
    }

    *out_handle = handle;
    return RESULT_OK;
}

static Result load_async_no_extra_params(AssetType type, const char *path, AssetLoadedFn on_loaded,
                                          void *userdata, AssetHandle **out_handle) {
    if (path == nullptr || out_handle == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return RESULT_ERROR_NOT_INITIALIZED;
    }

    AssetHandle *cached = cache_find(type, path);
    if (cached != nullptr) {
        cached->ref_count += 1;
        *out_handle = cached;
        return RESULT_OK;
    }

    AssetHandle *handle = calloc(1, sizeof(*handle));
    if (handle == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    handle->type = type;
    handle->status = ASSET_STATUS_LOADING;
    handle->ref_count = 1;
    snprintf(handle->source_path, sizeof(handle->source_path), "%s", path);
    handle->on_loaded = on_loaded;
    handle->on_loaded_userdata = userdata;

    cache_insert(type, path, handle);
    const Result result = push_pending(handle);
    if (result != RESULT_OK) {
        cache_remove(handle);
        free(handle);
        return result;
    }

    *out_handle = handle;
    return RESULT_OK;
}

Result asset_load_model_async(const char *path, AssetLoadedFn on_loaded, void *userdata,
                               AssetHandle **out_handle) {
    return load_async_no_extra_params(ASSET_TYPE_MODEL, path, on_loaded, userdata, out_handle);
}

Result asset_load_audio_data_async(const char *path, AssetLoadedFn on_loaded, void *userdata,
                                    AssetHandle **out_handle) {
    return load_async_no_extra_params(ASSET_TYPE_AUDIO_DATA, path, on_loaded, userdata, out_handle);
}

Result asset_load_font_data_async(const char *path, AssetLoadedFn on_loaded, void *userdata,
                                   AssetHandle **out_handle) {
    return load_async_no_extra_params(ASSET_TYPE_FONT_DATA, path, on_loaded, userdata, out_handle);
}

/* ==================================================================== *
 * Handle API
 * ==================================================================== */

void asset_retain(AssetHandle *handle) {
    ASSERT(handle != nullptr);
    if (handle != nullptr) {
        handle->ref_count += 1;
    }
}

void asset_release(AssetHandle *handle) {
    ASSERT(handle != nullptr);
    if (handle == nullptr) {
        return;
    }
    ASSERT_MSG(handle->ref_count > 0, "asset_release called more times than retained");
    if (handle->ref_count == 0) {
        return;
    }

    handle->ref_count -= 1;
    if (handle->ref_count > 0) {
        return;
    }

    cache_remove(handle);

    if (handle->status == ASSET_STATUS_LOADING) {
        /* A worker thread may still be decoding this handle; do not free
         * it out from under that thread. asset_manager_update() finishes
         * the job normally, notices this flag, and destroys it instead
         * of finalizing/notifying. */
        handle->pending_destroy = true;
        return;
    }

    destroy_handle(handle);
}

AssetType asset_get_type(const AssetHandle *handle) {
    ASSERT(handle != nullptr);
    return handle != nullptr ? handle->type : ASSET_TYPE_TEXTURE;
}

AssetStatus asset_get_status(const AssetHandle *handle) {
    ASSERT(handle != nullptr);
    return handle != nullptr ? handle->status : ASSET_STATUS_FAILED;
}

Texture *asset_get_texture(const AssetHandle *handle) {
    ASSERT(handle != nullptr);
    if (handle == nullptr || handle->type != ASSET_TYPE_TEXTURE ||
        handle->status != ASSET_STATUS_READY) {
        return nullptr;
    }
    return handle->texture;
}

ShaderProgram *asset_get_shader(const AssetHandle *handle) {
    ASSERT(handle != nullptr);
    if (handle == nullptr || handle->type != ASSET_TYPE_SHADER ||
        handle->status != ASSET_STATUS_READY) {
        return nullptr;
    }
    return handle->shader;
}

Result asset_get_model_meshes(const AssetHandle *handle, Mesh *const **out_meshes,
                               uint32_t *out_mesh_count) {
    if (handle == nullptr || out_meshes == nullptr || out_mesh_count == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (handle->type != ASSET_TYPE_MODEL) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (handle->status != ASSET_STATUS_READY) {
        return RESULT_ERROR_INVALID_STATE;
    }
    *out_meshes = (Mesh *const *)handle->model_meshes;
    *out_mesh_count = handle->model_mesh_count;
    return RESULT_OK;
}

Result asset_get_data(const AssetHandle *handle, const void **out_data, size_t *out_size) {
    if (handle == nullptr || out_data == nullptr || out_size == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (handle->type != ASSET_TYPE_AUDIO_DATA && handle->type != ASSET_TYPE_FONT_DATA) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (handle->status != ASSET_STATUS_READY) {
        return RESULT_ERROR_INVALID_STATE;
    }
    *out_data = handle->raw_data;
    *out_size = handle->raw_size;
    return RESULT_OK;
}

Result asset_reload(AssetHandle *handle) {
    ASSERT(handle != nullptr);
    if (handle == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (handle->status != ASSET_STATUS_READY) {
        return RESULT_ERROR_INVALID_STATE;
    }

    switch (handle->type) {
        case ASSET_TYPE_SHADER:
            return shader_program_reload(handle->shader);
        case ASSET_TYPE_TEXTURE:
            return texture_reload(handle->texture);
        case ASSET_TYPE_MODEL: {
            ModelCpuData cpu_data;
            Result       result = decode_model(handle->source_path, &cpu_data);
            if (result != RESULT_OK) {
                return result;
            }
            Mesh    **new_meshes = nullptr;
            uint32_t  new_count = 0;
            result = upload_model(&cpu_data, &new_meshes, &new_count);
            free_model_cpu_data(&cpu_data);
            if (result != RESULT_OK) {
                return result;
            }
            for (uint32_t i = 0; i < handle->model_mesh_count; ++i) {
                mesh_destroy(handle->model_meshes[i]);
            }
            free(handle->model_meshes);
            handle->model_meshes = new_meshes;
            handle->model_mesh_count = new_count;
            return RESULT_OK;
        }
        case ASSET_TYPE_AUDIO_DATA:
        case ASSET_TYPE_FONT_DATA: {
            void  *data = nullptr;
            size_t size = 0;
            const Result result = file_read_all(handle->source_path, &data, &size);
            if (result != RESULT_OK) {
                return result;
            }
            file_free_buffer(handle->raw_data);
            handle->raw_data = data;
            handle->raw_size = size;
            return RESULT_OK;
        }
        default:
            return RESULT_ERROR_NOT_SUPPORTED;
    }
}
