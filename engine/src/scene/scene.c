#include "eisenfront/scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCENE_NODE_NAME_CAPACITY 64u
#define SCENE_NODE_INDEX_BITS 20u
#define SCENE_NODE_INDEX_MASK ((1u << SCENE_NODE_INDEX_BITS) - 1u)
#define SCENE_MAX_GENERATION ((1u << (32u - SCENE_NODE_INDEX_BITS)) - 1u)

typedef struct SceneNode {
    uint32_t generation;
    bool     active;
    char     name[SCENE_NODE_NAME_CAPACITY];

    SceneNodeId parent;
    SceneNodeId first_child;
    SceneNodeId next_sibling;
    SceneNodeId prev_sibling;

    vec3   local_position;
    versor local_rotation;
    vec3   local_scale;
    mat4   world_matrix;
} SceneNode;

struct Scene {
    SceneNode  *nodes;      /* index 0 is permanently unused/reserved (index 0 == invalid) */
    uint32_t    capacity;   /* usable slots, indices 1..capacity */
    uint32_t    count;
    uint32_t   *free_indices;
    uint32_t    free_count;
    SceneNodeId root_first;
};

struct SceneManager {
    Scene *active;
};

static uint32_t id_index(SceneNodeId id) {
    return id & SCENE_NODE_INDEX_MASK;
}

static uint32_t id_generation(SceneNodeId id) {
    return id >> SCENE_NODE_INDEX_BITS;
}

static SceneNodeId make_id(uint32_t index, uint32_t generation) {
    return (SceneNodeId)((generation << SCENE_NODE_INDEX_BITS) | (index & SCENE_NODE_INDEX_MASK));
}

SceneNodeDesc scene_node_desc_default(void) {
    SceneNodeDesc desc;
    desc.parent = SCENE_INVALID_NODE_ID;
    desc.name = nullptr;
    desc.local_position[0] = 0.0f;
    desc.local_position[1] = 0.0f;
    desc.local_position[2] = 0.0f;
    glm_quat_identity(desc.local_rotation);
    desc.local_scale[0] = 1.0f;
    desc.local_scale[1] = 1.0f;
    desc.local_scale[2] = 1.0f;
    return desc;
}

Result scene_create(uint32_t max_nodes, Scene **out_scene) {
    if (max_nodes == 0 || max_nodes > SCENE_NODE_INDEX_MASK || out_scene == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    Scene *scene = malloc(sizeof(*scene));
    if (scene == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    /* +1: index 0 is reserved so SCENE_INVALID_NODE_ID (0) never
     * addresses a real slot. */
    scene->nodes = calloc((size_t)max_nodes + 1, sizeof(SceneNode));
    scene->free_indices = malloc(sizeof(uint32_t) * max_nodes);
    if (scene->nodes == nullptr || scene->free_indices == nullptr) {
        free(scene->nodes);
        free(scene->free_indices);
        free(scene);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    /* Push in descending order so index 1 is popped first - purely
     * cosmetic (any order is correct), but makes node IDs allocate in a
     * predictable, low-to-high sequence that is easier to read while
     * debugging. */
    for (uint32_t i = 0; i < max_nodes; ++i) {
        scene->free_indices[i] = max_nodes - i;
    }
    scene->free_count = max_nodes;
    scene->capacity = max_nodes;
    scene->count = 0;
    scene->root_first = SCENE_INVALID_NODE_ID;

    *out_scene = scene;
    return RESULT_OK;
}

void scene_destroy(Scene *scene) {
    if (scene == nullptr) {
        return;
    }
    free(scene->nodes);
    free(scene->free_indices);
    free(scene);
}

bool scene_node_is_valid(const Scene *scene, SceneNodeId node) {
    ASSERT(scene != nullptr);
    if (scene == nullptr || node == SCENE_INVALID_NODE_ID) {
        return false;
    }
    const uint32_t index = id_index(node);
    if (index == 0 || index > scene->capacity) {
        return false;
    }
    return scene->nodes[index].active && scene->nodes[index].generation == id_generation(node);
}

static void unlink_from_parent(Scene *scene, SceneNodeId id) {
    SceneNode *node = &scene->nodes[id_index(id)];

    if (node->prev_sibling != SCENE_INVALID_NODE_ID) {
        scene->nodes[id_index(node->prev_sibling)].next_sibling = node->next_sibling;
    } else if (node->parent != SCENE_INVALID_NODE_ID) {
        scene->nodes[id_index(node->parent)].first_child = node->next_sibling;
    } else {
        scene->root_first = node->next_sibling;
    }

    if (node->next_sibling != SCENE_INVALID_NODE_ID) {
        scene->nodes[id_index(node->next_sibling)].prev_sibling = node->prev_sibling;
    }

    node->prev_sibling = SCENE_INVALID_NODE_ID;
    node->next_sibling = SCENE_INVALID_NODE_ID;
}

static void link_as_child(Scene *scene, SceneNodeId id, SceneNodeId parent) {
    SceneNode   *node = &scene->nodes[id_index(id)];
    SceneNodeId *head =
        (parent != SCENE_INVALID_NODE_ID) ? &scene->nodes[id_index(parent)].first_child : &scene->root_first;

    node->parent = parent;
    node->next_sibling = *head;
    node->prev_sibling = SCENE_INVALID_NODE_ID;
    if (*head != SCENE_INVALID_NODE_ID) {
        scene->nodes[id_index(*head)].prev_sibling = id;
    }
    *head = id;
}

Result scene_node_create(Scene *scene, const SceneNodeDesc *desc, SceneNodeId *out_node) {
    if (scene == nullptr || desc == nullptr || out_node == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (desc->parent != SCENE_INVALID_NODE_ID && !scene_node_is_valid(scene, desc->parent)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (scene->free_count == 0) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }

    const uint32_t index = scene->free_indices[--scene->free_count];
    SceneNode     *node = &scene->nodes[index];

    node->active = true;
    snprintf(node->name, sizeof(node->name), "%s", (desc->name != nullptr) ? desc->name : "");
    node->first_child = SCENE_INVALID_NODE_ID;
    node->local_position[0] = desc->local_position[0];
    node->local_position[1] = desc->local_position[1];
    node->local_position[2] = desc->local_position[2];
    node->local_rotation[0] = desc->local_rotation[0];
    node->local_rotation[1] = desc->local_rotation[1];
    node->local_rotation[2] = desc->local_rotation[2];
    node->local_rotation[3] = desc->local_rotation[3];
    node->local_scale[0] = desc->local_scale[0];
    node->local_scale[1] = desc->local_scale[1];
    node->local_scale[2] = desc->local_scale[2];
    glm_mat4_identity(node->world_matrix);

    const SceneNodeId id = make_id(index, node->generation);
    link_as_child(scene, id, desc->parent);
    scene->count += 1;

    *out_node = id;
    return RESULT_OK;
}

static void destroy_subtree(Scene *scene, SceneNodeId id) {
    SceneNode  *node = &scene->nodes[id_index(id)];
    SceneNodeId child = node->first_child;
    while (child != SCENE_INVALID_NODE_ID) {
        const SceneNodeId next = scene->nodes[id_index(child)].next_sibling;
        destroy_subtree(scene, child);
        child = next;
    }

    unlink_from_parent(scene, id);
    node->active = false;
    node->generation = (node->generation < SCENE_MAX_GENERATION) ? node->generation + 1 : 0;
    scene->free_indices[scene->free_count++] = id_index(id);
    scene->count -= 1;
}

Result scene_node_destroy(Scene *scene, SceneNodeId node) {
    if (!scene_node_is_valid(scene, node)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    destroy_subtree(scene, node);
    return RESULT_OK;
}

/* Walks up from `descendant_candidate`'s parent chain; true if `node` is
 * found along the way, meaning `descendant_candidate` really is (or is
 * about to become) a descendant of `node`. */
static bool node_is_ancestor_of(const Scene *scene, SceneNodeId node, SceneNodeId descendant_candidate) {
    SceneNodeId current = descendant_candidate;
    while (current != SCENE_INVALID_NODE_ID) {
        if (current == node) {
            return true;
        }
        current = scene->nodes[id_index(current)].parent;
    }
    return false;
}

Result scene_node_set_parent(Scene *scene, SceneNodeId node, SceneNodeId new_parent) {
    if (!scene_node_is_valid(scene, node)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (new_parent == node) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (new_parent != SCENE_INVALID_NODE_ID) {
        if (!scene_node_is_valid(scene, new_parent)) {
            return RESULT_ERROR_INVALID_ARGUMENT;
        }
        /* Reparenting node under one of its own descendants would close
         * a cycle - see the file header comment. */
        if (node_is_ancestor_of(scene, node, new_parent)) {
            return RESULT_ERROR_INVALID_ARGUMENT;
        }
    }

    unlink_from_parent(scene, node);
    link_as_child(scene, node, new_parent);
    return RESULT_OK;
}

SceneNodeId scene_node_get_parent(const Scene *scene, SceneNodeId node) {
    if (!scene_node_is_valid(scene, node)) {
        return SCENE_INVALID_NODE_ID;
    }
    return scene->nodes[id_index(node)].parent;
}

uint32_t scene_node_get_child_count(const Scene *scene, SceneNodeId node) {
    if (!scene_node_is_valid(scene, node)) {
        return 0;
    }
    uint32_t    count = 0;
    SceneNodeId child = scene->nodes[id_index(node)].first_child;
    while (child != SCENE_INVALID_NODE_ID) {
        count += 1;
        child = scene->nodes[id_index(child)].next_sibling;
    }
    return count;
}

SceneNodeId scene_node_get_child_at(const Scene *scene, SceneNodeId node, uint32_t index) {
    if (!scene_node_is_valid(scene, node)) {
        return SCENE_INVALID_NODE_ID;
    }
    SceneNodeId child = scene->nodes[id_index(node)].first_child;
    uint32_t    i = 0;
    while (child != SCENE_INVALID_NODE_ID) {
        if (i == index) {
            return child;
        }
        child = scene->nodes[id_index(child)].next_sibling;
        i += 1;
    }
    return SCENE_INVALID_NODE_ID;
}

const char *scene_node_get_name(const Scene *scene, SceneNodeId node) {
    if (!scene_node_is_valid(scene, node)) {
        return nullptr;
    }
    return scene->nodes[id_index(node)].name;
}

Result scene_node_set_local_transform(Scene *scene, SceneNodeId node, vec3 position,
                                       versor rotation, vec3 scale) {
    if (!scene_node_is_valid(scene, node)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    SceneNode *n = &scene->nodes[id_index(node)];
    n->local_position[0] = position[0];
    n->local_position[1] = position[1];
    n->local_position[2] = position[2];
    n->local_rotation[0] = rotation[0];
    n->local_rotation[1] = rotation[1];
    n->local_rotation[2] = rotation[2];
    n->local_rotation[3] = rotation[3];
    n->local_scale[0] = scale[0];
    n->local_scale[1] = scale[1];
    n->local_scale[2] = scale[2];
    return RESULT_OK;
}

Result scene_node_get_local_position(const Scene *scene, SceneNodeId node, vec3 out_position) {
    if (!scene_node_is_valid(scene, node)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    glm_vec3_copy(scene->nodes[id_index(node)].local_position, out_position);
    return RESULT_OK;
}

Result scene_node_get_local_rotation(const Scene *scene, SceneNodeId node, versor out_rotation) {
    if (!scene_node_is_valid(scene, node)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    glm_quat_copy(scene->nodes[id_index(node)].local_rotation, out_rotation);
    return RESULT_OK;
}

Result scene_node_get_local_scale(const Scene *scene, SceneNodeId node, vec3 out_scale) {
    if (!scene_node_is_valid(scene, node)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    glm_vec3_copy(scene->nodes[id_index(node)].local_scale, out_scale);
    return RESULT_OK;
}

static void propagate(Scene *scene, SceneNodeId id, mat4 parent_world, bool has_parent) {
    SceneNode *node = &scene->nodes[id_index(id)];

    mat4 translation;
    mat4 rotation_matrix;
    mat4 scale_matrix;
    mat4 rotation_scale;
    mat4 local;
    glm_translate_make(translation, node->local_position);
    glm_quat_mat4(node->local_rotation, rotation_matrix);
    glm_scale_make(scale_matrix, node->local_scale);
    glm_mat4_mul(translation, rotation_matrix, rotation_scale);
    glm_mat4_mul(rotation_scale, scale_matrix, local);

    if (has_parent) {
        glm_mat4_mul(parent_world, local, node->world_matrix);
    } else {
        glm_mat4_copy(local, node->world_matrix);
    }

    SceneNodeId child = node->first_child;
    while (child != SCENE_INVALID_NODE_ID) {
        const SceneNodeId next = scene->nodes[id_index(child)].next_sibling;
        propagate(scene, child, node->world_matrix, true);
        child = next;
    }
}

void scene_update_transforms(Scene *scene) {
    ASSERT(scene != nullptr);
    if (scene == nullptr) {
        return;
    }
    mat4 identity;
    glm_mat4_identity(identity);

    SceneNodeId root = scene->root_first;
    while (root != SCENE_INVALID_NODE_ID) {
        const SceneNodeId next = scene->nodes[id_index(root)].next_sibling;
        propagate(scene, root, identity, false);
        root = next;
    }
}

Result scene_node_get_world_matrix(const Scene *scene, SceneNodeId node, mat4 out_world_matrix) {
    if (!scene_node_is_valid(scene, node)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    glm_mat4_copy(scene->nodes[id_index(node)].world_matrix, out_world_matrix);
    return RESULT_OK;
}

Result scene_manager_create(SceneManager **out_manager) {
    if (out_manager == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    SceneManager *manager = malloc(sizeof(*manager));
    if (manager == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    manager->active = nullptr;
    *out_manager = manager;
    return RESULT_OK;
}

void scene_manager_destroy(SceneManager *manager) {
    free(manager);
}

Scene *scene_manager_get_active(const SceneManager *manager) {
    ASSERT(manager != nullptr);
    return manager != nullptr ? manager->active : nullptr;
}

void scene_manager_transition_to(SceneManager *manager, Scene *new_scene,
                                  SceneTransitionFn on_unloaded, SceneTransitionFn on_loaded,
                                  void *userdata) {
    ASSERT(manager != nullptr);
    if (manager == nullptr) {
        return;
    }

    if (manager->active != nullptr && on_unloaded != nullptr) {
        on_unloaded(manager->active, userdata);
    }

    manager->active = new_scene;

    if (new_scene != nullptr && on_loaded != nullptr) {
        on_loaded(new_scene, userdata);
    }
}
