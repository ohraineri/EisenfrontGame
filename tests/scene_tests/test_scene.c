/*
 * Scene is a pure math/logic module (no GL, no window) - these tests run
 * with no video driver at all.
 */
#include "eisenfront/scene.h"

#include "unity.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_create_root_node(void) {
    Scene *scene = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_create(16, &scene));

    SceneNodeDesc desc = scene_node_desc_default();
    desc.name = "root";
    SceneNodeId root = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &desc, &root));

    TEST_ASSERT_TRUE(scene_node_is_valid(scene, root));
    TEST_ASSERT_EQUAL(SCENE_INVALID_NODE_ID, scene_node_get_parent(scene, root));
    TEST_ASSERT_EQUAL(0, scene_node_get_child_count(scene, root));
    TEST_ASSERT_EQUAL_STRING("root", scene_node_get_name(scene, root));

    scene_destroy(scene);
}

static void test_child_hierarchy(void) {
    Scene *scene = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_create(16, &scene));

    SceneNodeDesc root_desc = scene_node_desc_default();
    SceneNodeId   root = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &root_desc, &root));

    SceneNodeDesc child_desc = scene_node_desc_default();
    child_desc.parent = root;
    SceneNodeId child = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &child_desc, &child));

    TEST_ASSERT_EQUAL(root, scene_node_get_parent(scene, child));
    TEST_ASSERT_EQUAL(1, scene_node_get_child_count(scene, root));
    TEST_ASSERT_EQUAL(child, scene_node_get_child_at(scene, root, 0));

    scene_destroy(scene);
}

static void test_destroying_parent_destroys_subtree(void) {
    Scene *scene = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_create(16, &scene));

    SceneNodeDesc root_desc = scene_node_desc_default();
    SceneNodeId   root = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &root_desc, &root));

    SceneNodeDesc child_desc = scene_node_desc_default();
    child_desc.parent = root;
    SceneNodeId child = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &child_desc, &child));

    SceneNodeDesc grandchild_desc = scene_node_desc_default();
    grandchild_desc.parent = child;
    SceneNodeId grandchild = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &grandchild_desc, &grandchild));

    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_destroy(scene, root));

    TEST_ASSERT_FALSE(scene_node_is_valid(scene, root));
    TEST_ASSERT_FALSE(scene_node_is_valid(scene, child));
    TEST_ASSERT_FALSE(scene_node_is_valid(scene, grandchild));

    scene_destroy(scene);
}

static void test_stale_id_detected_after_slot_reuse(void) {
    Scene *scene = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_create(1, &scene));

    SceneNodeDesc desc = scene_node_desc_default();
    SceneNodeId   first = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &desc, &first));
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_destroy(scene, first));

    SceneNodeId second = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &desc, &second));

    /* Same slot (capacity 1, so it must be reused), but the generation
     * bump makes the two IDs distinguishable. */
    TEST_ASSERT_NOT_EQUAL(first, second);
    TEST_ASSERT_FALSE(scene_node_is_valid(scene, first));
    TEST_ASSERT_TRUE(scene_node_is_valid(scene, second));

    scene_destroy(scene);
}

static void test_capacity_exceeded(void) {
    Scene *scene = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_create(1, &scene));

    SceneNodeDesc desc = scene_node_desc_default();
    SceneNodeId   a = SCENE_INVALID_NODE_ID;
    SceneNodeId   b = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &desc, &a));
    TEST_ASSERT_EQUAL(RESULT_ERROR_CAPACITY_EXCEEDED, scene_node_create(scene, &desc, &b));

    scene_destroy(scene);
}

static void test_set_parent_rejects_self(void) {
    Scene *scene = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_create(4, &scene));

    SceneNodeDesc desc = scene_node_desc_default();
    SceneNodeId   node = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &desc, &node));

    TEST_ASSERT_EQUAL(RESULT_ERROR_INVALID_ARGUMENT, scene_node_set_parent(scene, node, node));

    scene_destroy(scene);
}

static void test_set_parent_rejects_cycle_through_descendant(void) {
    Scene *scene = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_create(8, &scene));

    SceneNodeDesc desc = scene_node_desc_default();
    SceneNodeId   a = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &desc, &a));

    desc.parent = a;
    SceneNodeId b = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &desc, &b));

    desc.parent = b;
    SceneNodeId c = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &desc, &c));

    /* A -> B -> C already; making A a child of C would close a cycle. */
    TEST_ASSERT_EQUAL(RESULT_ERROR_INVALID_ARGUMENT, scene_node_set_parent(scene, a, c));
    /* Hierarchy must be unchanged after the rejected attempt. */
    TEST_ASSERT_EQUAL(SCENE_INVALID_NODE_ID, scene_node_get_parent(scene, a));
    TEST_ASSERT_EQUAL(a, scene_node_get_parent(scene, b));

    scene_destroy(scene);
}

static void test_transform_propagation_combines_parent_and_child(void) {
    Scene *scene = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_create(4, &scene));

    SceneNodeDesc root_desc = scene_node_desc_default();
    root_desc.local_position[0] = 1.0f;
    SceneNodeId root = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &root_desc, &root));

    SceneNodeDesc child_desc = scene_node_desc_default();
    child_desc.parent = root;
    child_desc.local_position[1] = 1.0f;
    SceneNodeId child = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &child_desc, &child));

    scene_update_transforms(scene);

    mat4 world;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_get_world_matrix(scene, child, world));

    vec3 origin = {0.0f, 0.0f, 0.0f};
    vec3 world_point;
    glm_mat4_mulv3(world, origin, 1.0f, world_point);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, world_point[0]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, world_point[1]);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, world_point[2]);

    scene_destroy(scene);
}

static void test_reparent_changes_world_transform_after_update(void) {
    Scene *scene = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_create(4, &scene));

    SceneNodeDesc a_desc = scene_node_desc_default();
    a_desc.local_position[0] = 10.0f;
    SceneNodeId a = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &a_desc, &a));

    SceneNodeDesc leaf_desc = scene_node_desc_default();
    SceneNodeId   leaf = SCENE_INVALID_NODE_ID;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_create(scene, &leaf_desc, &leaf)); /* root, at origin */

    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_set_parent(scene, leaf, a));
    scene_update_transforms(scene);

    mat4 world;
    vec3 origin = {0.0f, 0.0f, 0.0f};
    vec3 world_point;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_node_get_world_matrix(scene, leaf, world));
    glm_mat4_mulv3(world, origin, 1.0f, world_point);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 10.0f, world_point[0]);

    scene_destroy(scene);
}

static int  g_unload_calls;
static int  g_load_calls;
static Scene *g_last_unloaded;
static Scene *g_last_loaded;

static void on_unloaded(Scene *scene, void *userdata) {
    (void)userdata;
    g_unload_calls += 1;
    g_last_unloaded = scene;
}

static void on_loaded(Scene *scene, void *userdata) {
    (void)userdata;
    g_load_calls += 1;
    g_last_loaded = scene;
}

static void test_scene_manager_transitions(void) {
    g_unload_calls = 0;
    g_load_calls = 0;
    g_last_unloaded = nullptr;
    g_last_loaded = nullptr;

    SceneManager *manager = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_manager_create(&manager));
    TEST_ASSERT_NULL(scene_manager_get_active(manager));

    Scene *scene_a = nullptr;
    Scene *scene_b = nullptr;
    TEST_ASSERT_EQUAL(RESULT_OK, scene_create(1, &scene_a));
    TEST_ASSERT_EQUAL(RESULT_OK, scene_create(1, &scene_b));

    scene_manager_transition_to(manager, scene_a, on_unloaded, on_loaded, nullptr);
    TEST_ASSERT_EQUAL(scene_a, scene_manager_get_active(manager));
    TEST_ASSERT_EQUAL(0, g_unload_calls); /* no previous scene to unload */
    TEST_ASSERT_EQUAL(1, g_load_calls);
    TEST_ASSERT_EQUAL(scene_a, g_last_loaded);

    scene_manager_transition_to(manager, scene_b, on_unloaded, on_loaded, nullptr);
    TEST_ASSERT_EQUAL(scene_b, scene_manager_get_active(manager));
    TEST_ASSERT_EQUAL(1, g_unload_calls);
    TEST_ASSERT_EQUAL(scene_a, g_last_unloaded);
    TEST_ASSERT_EQUAL(2, g_load_calls);
    TEST_ASSERT_EQUAL(scene_b, g_last_loaded);

    scene_manager_destroy(manager);
    scene_destroy(scene_a);
    scene_destroy(scene_b);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_create_root_node);
    RUN_TEST(test_child_hierarchy);
    RUN_TEST(test_destroying_parent_destroys_subtree);
    RUN_TEST(test_stale_id_detected_after_slot_reuse);
    RUN_TEST(test_capacity_exceeded);
    RUN_TEST(test_set_parent_rejects_self);
    RUN_TEST(test_set_parent_rejects_cycle_through_descendant);
    RUN_TEST(test_transform_propagation_combines_parent_and_child);
    RUN_TEST(test_reparent_changes_world_transform_after_update);
    RUN_TEST(test_scene_manager_transitions);

    return UNITY_END();
}
