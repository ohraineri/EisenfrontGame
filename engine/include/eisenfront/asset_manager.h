/*
 * Asset Manager - a single path-keyed, reference-counted cache and
 * handle system spanning every loadable asset kind (models, textures,
 * shaders, audio data, font data), plus a background async loading
 * architecture. Never loads the same (type, path) twice.
 *
 * Materials are deliberately not an asset type yet: the Material module
 * does not exist until a later engine pass. When it does, it plugs into
 * this same handle/cache/refcount machinery the way ASSET_TYPE_MODEL
 * plugs in today - Asset Manager's cache is generic over "a type tag
 * plus a path", not hardcoded to five kinds forever.
 *
 * Delegation vs. ownership: for ASSET_TYPE_TEXTURE and ASSET_TYPE_SHADER,
 * Asset Manager delegates the actual load to texture_load_2d() /
 * shader_program_load() (see texture.h, shader.h), which already cache
 * and reference-count by path themselves; the AssetHandle here is a
 * uniform wrapper around whatever those return, forwarding retain/
 * release and reload to them. For ASSET_TYPE_MODEL (glTF geometry via
 * cgltf) and ASSET_TYPE_AUDIO_DATA / ASSET_TYPE_FONT_DATA (raw file
 * bytes), there is no lower-level module to delegate to, so Asset
 * Manager owns that caching and loading directly.
 *
 * Model loading scope: a model is imported as one Mesh per glTF
 * primitive (position/normal/tangent/UV, indices where present),
 * across every mesh in the file. It does not yet import materials,
 * textures, skinning weights beyond the default, or animations - the
 * Material module (a later pass) is what a model's meshes get paired
 * with texture/material bindings through, following the same pattern
 * Renderer deferred lighting and Graphics Context deferred rendering.
 *
 * Asynchronous loading architecture: asset_load_*_async() enqueues a
 * job and returns immediately with a handle in ASSET_STATUS_LOADING.
 * Background worker threads (created at asset_manager_init()) run only
 * the pure-CPU half of a load - file I/O, stb_image decoding, cgltf
 * parsing - none of which touches OpenGL, so it is safe off the main
 * thread. The GPU-touching half (texture upload, Mesh/VAO creation)
 * only ever runs on the main thread, inside asset_manager_update(),
 * which must be called once per frame; that call is also what fires
 * on_loaded callbacks and flips a handle's status to
 * ASSET_STATUS_READY or ASSET_STATUS_FAILED. Shader compilation has no
 * async path: compiling a shader is itself a GL call
 * (glCompileShader), so unlike texture/model decoding there is no
 * separable CPU-only half to move to a worker thread - asset_load_shader()
 * is synchronous only.
 *
 * Dependencies: Core, Platform (file I/O, threads, mutexes), Graphics
 * Context, Shader, Texture, Mesh, and cgltf (privately, in
 * asset_manager.c only, for model parsing).
 */
#ifndef EISENFRONT_ASSET_MANAGER_H
#define EISENFRONT_ASSET_MANAGER_H

#include "core.h"
#include "mesh.h"
#include "shader.h"
#include "texture.h"

/* worker_thread_count must be at least 1. */
ENGINE_API Result asset_manager_init(uint32_t worker_thread_count);
ENGINE_API void   asset_manager_shutdown(void);

/* Call exactly once per frame, on the main thread; see the file header
 * comment on the async architecture. */
ENGINE_API void asset_manager_update(void);

typedef enum AssetType {
    ASSET_TYPE_TEXTURE = 0,
    ASSET_TYPE_SHADER,
    ASSET_TYPE_MODEL,
    ASSET_TYPE_AUDIO_DATA,
    ASSET_TYPE_FONT_DATA,
} AssetType;

typedef enum AssetStatus {
    ASSET_STATUS_LOADING = 0,
    ASSET_STATUS_READY,
    ASSET_STATUS_FAILED,
} AssetStatus;

typedef struct AssetHandle AssetHandle;

/* Fires from inside asset_manager_update(), on the main thread - never
 * from a worker thread. Not fired for a call that returns an existing
 * cached handle (check asset_get_status() immediately in that case). */
typedef void (*AssetLoadedFn)(AssetHandle *handle, void *userdata);

/* ---- Synchronous loads: block the calling thread until done ---- */

ENGINE_API Result asset_load_texture(const char *path, const TextureParams *params,
                                      AssetHandle **out_handle);
ENGINE_API Result asset_load_shader(const ShaderProgramDesc *desc, AssetHandle **out_handle);
ENGINE_API Result asset_load_model(const char *path, AssetHandle **out_handle);
ENGINE_API Result asset_load_audio_data(const char *path, AssetHandle **out_handle);
ENGINE_API Result asset_load_font_data(const char *path, AssetHandle **out_handle);

/* ---- Asynchronous loads: return immediately in ASSET_STATUS_LOADING ---- */

ENGINE_API Result asset_load_texture_async(const char *path, const TextureParams *params,
                                            AssetLoadedFn on_loaded, void *userdata,
                                            AssetHandle **out_handle);
ENGINE_API Result asset_load_model_async(const char *path, AssetLoadedFn on_loaded, void *userdata,
                                          AssetHandle **out_handle);
ENGINE_API Result asset_load_audio_data_async(const char *path, AssetLoadedFn on_loaded,
                                               void *userdata, AssetHandle **out_handle);
ENGINE_API Result asset_load_font_data_async(const char *path, AssetLoadedFn on_loaded,
                                              void *userdata, AssetHandle **out_handle);

ENGINE_API void asset_retain(AssetHandle *handle);
ENGINE_API void asset_release(AssetHandle *handle);

ENGINE_API AssetType   asset_get_type(const AssetHandle *handle);
ENGINE_API AssetStatus asset_get_status(const AssetHandle *handle);

/* Each of these returns nullptr / RESULT_ERROR_INVALID_STATE unless the
 * handle matches that asset type and its status is ASSET_STATUS_READY. */
ENGINE_API Texture       *asset_get_texture(const AssetHandle *handle);
ENGINE_API ShaderProgram *asset_get_shader(const AssetHandle *handle);
ENGINE_API Result asset_get_model_meshes(const AssetHandle *handle, Mesh *const **out_meshes,
                                          uint32_t *out_mesh_count);
ENGINE_API Result asset_get_data(const AssetHandle *handle, const void **out_data,
                                  size_t *out_size);

/* Re-loads from the original source path in place, synchronously - see
 * shader_program_reload() / texture_reload() for what "in place" means
 * for those two types. Not available for assets with no source path
 * (there are none reachable through this API: every asset_load_*
 * function requires a path). Returns RESULT_ERROR_NOT_SUPPORTED for
 * asset types that do not support it. */
ENGINE_API Result asset_reload(AssetHandle *handle);

#endif /* EISENFRONT_ASSET_MANAGER_H */
