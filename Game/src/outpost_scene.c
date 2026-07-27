#include "outpost_scene.h"

#include "primitives.h"

typedef struct PendingObject {
    SceneNodeId node;
    vec3        half_extents;
    Material   *material;
    float       uv_scale;
    SurfaceType surface_type;
} PendingObject;

typedef struct BuildContext {
    Scene         *scene;
    PhysicsWorld  *physics_world;
    PendingObject  pending[OUTPOST_MAX_STATIC_OBJECTS];
    uint32_t       pending_count;
} BuildContext;

static SceneNodeId add_group(BuildContext *ctx, SceneNodeId parent, vec3 local_position) {
    SceneNodeDesc desc = scene_node_desc_default();
    desc.parent = parent;
    desc.local_position[0] = local_position[0];
    desc.local_position[1] = local_position[1];
    desc.local_position[2] = local_position[2];
    SceneNodeId node = SCENE_INVALID_NODE_ID;
    scene_node_create(ctx->scene, &desc, &node);
    return node;
}

static void add_box(BuildContext *ctx, SceneNodeId parent, vec3 local_position, vec3 half_extents,
                     Material *material, float uv_scale, SurfaceType surface_type) {
    if (ctx->pending_count >= OUTPOST_MAX_STATIC_OBJECTS) {
        return;
    }
    SceneNodeDesc desc = scene_node_desc_default();
    desc.parent = parent;
    desc.local_position[0] = local_position[0];
    desc.local_position[1] = local_position[1];
    desc.local_position[2] = local_position[2];
    SceneNodeId node = SCENE_INVALID_NODE_ID;
    scene_node_create(ctx->scene, &desc, &node);

    PendingObject *pending = &ctx->pending[ctx->pending_count++];
    pending->node = node;
    pending->half_extents[0] = half_extents[0];
    pending->half_extents[1] = half_extents[1];
    pending->half_extents[2] = half_extents[2];
    pending->material = material;
    pending->uv_scale = uv_scale;
    pending->surface_type = surface_type;
}

/* HESCO-style perimeter barrier segments along a straight line from
 * `start` to `end`, `spacing` apart, skipping any segment whose
 * position along the line falls within `gap_center` +/- `gap_half`
 * (the checkpoint entrance). `long_axis_x` selects whether each
 * segment's long side runs along world X (north/south walls) or world
 * Z (east/west walls). */
static void add_barrier_line(BuildContext *ctx, SceneNodeId parent, vec3 start, vec3 end,
                              float spacing, float gap_center, float gap_half, bool long_axis_x,
                              Material *barrier_material, SurfaceType surface_type) {
    const float dx = end[0] - start[0];
    const float dz = end[2] - start[2];
    const float length = long_axis_x ? dx : dz;
    const int   count = (int)(length / spacing);
    vec3        half_extents;
    if (long_axis_x) {
        half_extents[0] = 1.0f;
        half_extents[1] = 0.6f;
        half_extents[2] = 0.5f;
    } else {
        half_extents[0] = 0.5f;
        half_extents[1] = 0.6f;
        half_extents[2] = 1.0f;
    }

    for (int i = 0; i <= count; ++i) {
        const float t = (float)i / (float)count;
        const float x = start[0] + dx * t;
        const float z = start[2] + dz * t;
        const float along = long_axis_x ? x : z;
        if (along > gap_center - gap_half && along < gap_center + gap_half) {
            continue;
        }
        add_box(ctx, parent, (vec3){x, half_extents[1], z}, half_extents, barrier_material, 0.6f,
                surface_type);
    }
}

static void add_watchtower(BuildContext *ctx, SceneNodeId parent, vec3 base_position,
                            Material *watchtower_material) {
    const float leg_half_height = 2.5f;
    vec3        leg_half_extents = {0.15f, leg_half_height, 0.15f};
    const float footprint = 1.3f;
    vec3        leg_offsets[4] = {
        {footprint, leg_half_height, footprint},
        {-footprint, leg_half_height, footprint},
        {footprint, leg_half_height, -footprint},
        {-footprint, leg_half_height, -footprint},
    };
    for (int i = 0; i < 4; ++i) {
        vec3 leg_position = {base_position[0] + leg_offsets[i][0], leg_offsets[i][1],
                              base_position[2] + leg_offsets[i][2]};
        add_box(ctx, parent, leg_position, leg_half_extents, watchtower_material, 1.0f,
                SURFACE_TYPE_WOOD);
    }

    vec3 platform_half_extents = {2.0f, 0.15f, 2.0f};
    vec3 platform_position = {base_position[0], leg_half_height * 2.0f + platform_half_extents[1],
                               base_position[2]};
    add_box(ctx, parent, platform_position, platform_half_extents, watchtower_material, 1.0f,
            SURFACE_TYPE_WOOD);
}

Result outpost_level_create(ShaderProgram *lit_shader, PhysicsWorld *physics_world,
                             OutpostLevel *out_level) {
    if (lit_shader == nullptr || physics_world == nullptr || out_level == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    BuildContext ctx = {0};
    ctx.physics_world = physics_world;

    Result result = scene_create(256u, &ctx.scene);
    if (result != RESULT_OK) {
        return result;
    }

    MaterialParams barrier_params = material_params_default();
    barrier_params.base_color[0] = 0.55f;
    barrier_params.base_color[1] = 0.50f;
    barrier_params.base_color[2] = 0.38f;
    const MaterialDesc barrier_desc = {.shader = lit_shader, .textures = {0}, .params = barrier_params};
    Material *barrier_material = nullptr;
    material_create(&barrier_desc, &barrier_material);

    MaterialParams building_params = material_params_default();
    building_params.base_color[0] = 0.62f;
    building_params.base_color[1] = 0.60f;
    building_params.base_color[2] = 0.56f;
    const MaterialDesc building_desc = {.shader = lit_shader, .textures = {0}, .params = building_params};
    Material *building_material = nullptr;
    material_create(&building_desc, &building_material);

    MaterialParams shed_params = material_params_default();
    shed_params.base_color[0] = 0.45f;
    shed_params.base_color[1] = 0.44f;
    shed_params.base_color[2] = 0.40f;
    const MaterialDesc shed_desc = {.shader = lit_shader, .textures = {0}, .params = shed_params};
    Material *shed_material = nullptr;
    material_create(&shed_desc, &shed_material);

    MaterialParams watchtower_params = material_params_default();
    watchtower_params.base_color[0] = 0.30f;
    watchtower_params.base_color[1] = 0.24f;
    watchtower_params.base_color[2] = 0.17f;
    const MaterialDesc watchtower_desc = {
        .shader = lit_shader, .textures = {0}, .params = watchtower_params};
    Material *watchtower_material = nullptr;
    material_create(&watchtower_desc, &watchtower_material);

    MaterialParams prop_params = material_params_default();
    prop_params.base_color[0] = 0.40f;
    prop_params.base_color[1] = 0.30f;
    prop_params.base_color[2] = 0.20f;
    const MaterialDesc prop_desc = {.shader = lit_shader, .textures = {0}, .params = prop_params};
    Material *prop_material = nullptr;
    material_create(&prop_desc, &prop_material);

    SceneNodeDesc root_desc = scene_node_desc_default();
    SceneNodeId   root = SCENE_INVALID_NODE_ID;
    scene_node_create(ctx.scene, &root_desc, &root);

    /* Perimeter: a 40x40m defensive square, HESCO-style barrier
     * segments on all four sides, a 6m gap centered on the south wall
     * for the entrance/checkpoint. */
    const SceneNodeId perimeter_group = add_group(&ctx, root, (vec3){0.0f, 0.0f, 0.0f});
    const float        half_size = 20.0f;
    add_barrier_line(&ctx, perimeter_group, (vec3){-half_size, 0.0f, half_size},
                      (vec3){half_size, 0.0f, half_size}, 2.2f, 0.0f, 0.0f, true, barrier_material,
                      SURFACE_TYPE_GRAVEL);
    add_barrier_line(&ctx, perimeter_group, (vec3){-half_size, 0.0f, -half_size},
                      (vec3){half_size, 0.0f, -half_size}, 2.2f, 0.0f, 3.0f, true, barrier_material,
                      SURFACE_TYPE_GRAVEL);
    add_barrier_line(&ctx, perimeter_group, (vec3){half_size, 0.0f, -half_size},
                      (vec3){half_size, 0.0f, half_size}, 2.2f, 0.0f, 0.0f, false, barrier_material,
                      SURFACE_TYPE_GRAVEL);
    add_barrier_line(&ctx, perimeter_group, (vec3){-half_size, 0.0f, -half_size},
                      (vec3){-half_size, 0.0f, half_size}, 2.2f, 0.0f, 0.0f, false, barrier_material,
                      SURFACE_TYPE_GRAVEL);

    /* Watchtower just inside the entrance, covering the southern
     * approach - the whole reason a checkpoint has one. */
    const SceneNodeId watchtower_group = add_group(&ctx, root, (vec3){0.0f, 0.0f, -16.0f});
    add_watchtower(&ctx, watchtower_group, (vec3){0.0f, 0.0f, 0.0f}, watchtower_material);

    /* Structures cluster: a real parent-child transform composition -
     * moving structures_group would move both buildings together. */
    const SceneNodeId structures_group = add_group(&ctx, root, (vec3){0.0f, 0.0f, 10.0f});
    add_box(&ctx, structures_group, (vec3){-8.0f, 2.0f, -2.0f}, (vec3){4.0f, 2.0f, 3.0f},
            building_material, 0.4f, SURFACE_TYPE_CONCRETE);
    add_box(&ctx, structures_group, (vec3){8.0f, 1.5f, -4.0f}, (vec3){2.5f, 1.5f, 2.5f}, shed_material,
            0.4f, SURFACE_TYPE_METAL);

    /* Scattered props for detail: crates by the shed, sandbag piles
     * near the checkpoint, barrels near the command building. */
    const SceneNodeId props_group = add_group(&ctx, root, (vec3){0.0f, 0.0f, 0.0f});
    vec3 crate_positions[4] = {
        {11.5f, 0.4f, 6.5f}, {12.3f, 0.4f, 5.2f}, {10.8f, 0.4f, 4.0f}, {12.0f, 1.2f, 5.5f}};
    for (int i = 0; i < 4; ++i) {
        add_box(&ctx, props_group, crate_positions[i], (vec3){0.4f, 0.4f, 0.4f}, prop_material, 1.0f,
                SURFACE_TYPE_WOOD);
    }
    vec3 sandbag_positions[3] = {{-3.0f, 0.3f, -17.5f}, {3.0f, 0.3f, -17.5f}, {0.0f, 0.3f, -14.0f}};
    for (int i = 0; i < 3; ++i) {
        add_box(&ctx, props_group, sandbag_positions[i], (vec3){1.0f, 0.3f, 0.4f}, barrier_material,
                0.8f, SURFACE_TYPE_SAND);
    }
    vec3 barrel_positions[3] = {{-11.0f, 0.5f, 9.0f}, {-10.2f, 0.5f, 10.5f}, {-12.0f, 0.5f, 7.5f}};
    for (int i = 0; i < 3; ++i) {
        add_box(&ctx, props_group, barrel_positions[i], (vec3){0.3f, 0.5f, 0.3f}, prop_material, 1.0f,
                SURFACE_TYPE_METAL);
    }

    scene_update_transforms(ctx.scene);

    out_level->scene = ctx.scene;
    out_level->barrier_material = barrier_material;
    out_level->building_material = building_material;
    out_level->shed_material = shed_material;
    out_level->watchtower_material = watchtower_material;
    out_level->prop_material = prop_material;
    out_level->object_count = 0;

    for (uint32_t i = 0; i < ctx.pending_count; ++i) {
        PendingObject *pending = &ctx.pending[i];
        mat4           world_matrix;
        scene_node_get_world_matrix(ctx.scene, pending->node, world_matrix);
        vec3 world_position = {world_matrix[3][0], world_matrix[3][1], world_matrix[3][2]};

        Mesh *mesh = nullptr;
        if (primitive_create_box(world_position, pending->half_extents, pending->uv_scale, &mesh) !=
            RESULT_OK) {
            continue;
        }

        RigidBodyDesc body_desc = rigid_body_desc_default();
        body_desc.type = BODY_TYPE_STATIC;
        body_desc.shape = shape_box(pending->half_extents);
        body_desc.position[0] = world_position[0];
        body_desc.position[1] = world_position[1];
        body_desc.position[2] = world_position[2];
        BodyId body = PHYSICS_INVALID_BODY_ID;
        rigid_body_create(physics_world, &body_desc, &body);

        StaticObject *object = &out_level->objects[out_level->object_count++];
        object->mesh = mesh;
        object->material = pending->material;
        object->body = body;
        object->surface_type = pending->surface_type;
    }

    return RESULT_OK;
}

SurfaceType outpost_level_surface_type_for_body(const OutpostLevel *level, BodyId body) {
    for (uint32_t i = 0; i < level->object_count; ++i) {
        if (level->objects[i].body == body) {
            return level->objects[i].surface_type;
        }
    }
    return SURFACE_TYPE_SOIL;
}

void outpost_level_destroy(OutpostLevel *level, PhysicsWorld *physics_world) {
    if (level == nullptr) {
        return;
    }
    for (uint32_t i = 0; i < level->object_count; ++i) {
        mesh_destroy(level->objects[i].mesh);
        if (physics_world != nullptr) {
            rigid_body_destroy(physics_world, level->objects[i].body);
        }
    }
    material_release(level->prop_material);
    material_release(level->watchtower_material);
    material_release(level->shed_material);
    material_release(level->building_material);
    material_release(level->barrier_material);
    scene_destroy(level->scene);
}
