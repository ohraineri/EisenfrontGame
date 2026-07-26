/*
 * Scene - a hierarchy of transform nodes (position/rotation/scale, with
 * children inheriting their parent's transform), multiple independent
 * scenes, and a SceneManager for switching which one is active.
 *
 * SceneNodeId is a generational handle, not a pointer: the low 20 bits
 * are a slot index into the Scene's node array, the high 12 bits are
 * that slot's generation counter. Destroying a node frees its slot for
 * reuse and bumps the generation, so a stale SceneNodeId from before the
 * destroy is detected as invalid (scene_node_is_valid() returns false)
 * instead of silently referring to whatever unrelated node was later
 * created in the same slot.
 *
 * Circular references: scene_node_set_parent() walks the candidate new
 * parent's ancestor chain and rejects the reparent (RESULT_ERROR_INVALID_ARGUMENT)
 * if the node being reparented is among its own ancestors-to-be - i.e.
 * making a node a child of its own descendant, which is the only way a
 * pure tree (as opposed to a general graph) could develop a cycle. A
 * node also cannot be parented to itself.
 *
 * Transform propagation: scene_update_transforms() walks every scene
 * from its root nodes downward, computing each node's world matrix as
 * parent_world * local (local = translation * rotation * scale, in that
 * order) - so a child's world position/rotation/scale always account
 * for every ancestor's transform, not just its immediate parent.
 * scene_node_get_world_matrix() returns whatever was computed by the
 * most recent scene_update_transforms() call, so call it once per frame
 * before reading world matrices (exactly like Renderer's begin/end
 * frame contract).
 *
 * Scene loading/unloading here means constructing/populating a Scene
 * object and its node hierarchy and later destroying it - there is no
 * on-disk scene file format defined anywhere in this engine yet, so
 * "loading" is not deserialization. SceneManager's job is scene
 * transitions: tracking which Scene is currently active and running
 * unload/load hooks when that changes.
 *
 * Dependencies: Core and cglm (publicly, like Camera).
 */
#ifndef EISENFRONT_SCENE_H
#define EISENFRONT_SCENE_H

#include "core.h"

#include <cglm/cglm.h>

typedef uint32_t SceneNodeId;
#define SCENE_INVALID_NODE_ID ((SceneNodeId)0)

typedef struct SceneNodeDesc {
    SceneNodeId parent; /* SCENE_INVALID_NODE_ID for a root node */
    const char *name;   /* copied; may be nullptr */
    vec3        local_position;
    versor      local_rotation; /* identity: {0, 0, 0, 1} */
    vec3        local_scale;    /* identity: {1, 1, 1} */
} SceneNodeDesc;

ENGINE_API SceneNodeDesc scene_node_desc_default(void);

typedef struct Scene Scene;

ENGINE_API Result scene_create(uint32_t max_nodes, Scene **out_scene);
ENGINE_API void   scene_destroy(Scene *scene);

ENGINE_API Result scene_node_create(Scene *scene, const SceneNodeDesc *desc,
                                     SceneNodeId *out_node);
/* Destroys node and its entire subtree. */
ENGINE_API Result scene_node_destroy(Scene *scene, SceneNodeId node);

ENGINE_API bool scene_node_is_valid(const Scene *scene, SceneNodeId node);

ENGINE_API Result      scene_node_set_parent(Scene *scene, SceneNodeId node,
                                              SceneNodeId new_parent);
ENGINE_API SceneNodeId scene_node_get_parent(const Scene *scene, SceneNodeId node);

ENGINE_API uint32_t    scene_node_get_child_count(const Scene *scene, SceneNodeId node);
ENGINE_API SceneNodeId scene_node_get_child_at(const Scene *scene, SceneNodeId node,
                                                uint32_t index);

ENGINE_API const char *scene_node_get_name(const Scene *scene, SceneNodeId node);

ENGINE_API Result scene_node_set_local_transform(Scene *scene, SceneNodeId node, vec3 position,
                                                  versor rotation, vec3 scale);
ENGINE_API Result scene_node_get_local_position(const Scene *scene, SceneNodeId node,
                                                 vec3 out_position);
ENGINE_API Result scene_node_get_local_rotation(const Scene *scene, SceneNodeId node,
                                                 versor out_rotation);
ENGINE_API Result scene_node_get_local_scale(const Scene *scene, SceneNodeId node,
                                              vec3 out_scale);

/* Recomputes every node's cached world matrix; call once per frame
 * before reading them. */
ENGINE_API void scene_update_transforms(Scene *scene);
/* Valid only as of the most recent scene_update_transforms() call. */
ENGINE_API Result scene_node_get_world_matrix(const Scene *scene, SceneNodeId node,
                                               mat4 out_world_matrix);

typedef struct SceneManager SceneManager;

ENGINE_API Result scene_manager_create(SceneManager **out_manager);
/* Does not destroy the active scene - the caller still owns it. */
ENGINE_API void   scene_manager_destroy(SceneManager *manager);

ENGINE_API Scene *scene_manager_get_active(const SceneManager *manager);

typedef void (*SceneTransitionFn)(Scene *scene, void *userdata);

/* Sets new_scene active, calling on_unloaded(previous_active, userdata)
 * first (skipped if there was no previous active scene) and then
 * on_loaded(new_scene, userdata) (skipped if new_scene is nullptr).
 * Either callback may be nullptr to skip it. Does not destroy the
 * previous scene - same ownership rule as scene_manager_destroy(). */
ENGINE_API void scene_manager_transition_to(SceneManager *manager, Scene *new_scene,
                                             SceneTransitionFn on_unloaded,
                                             SceneTransitionFn on_loaded, void *userdata);

#endif /* EISENFRONT_SCENE_H */
