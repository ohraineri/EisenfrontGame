#include "ai_soldier.h"

#include "primitives.h"

#include <math.h>

typedef struct PatrolContext {
    AiSoldierSquad *squad;
    float            delta_seconds;
} PatrolContext;

static void patrol_query_callback(Entity entity, void **components, void *userdata) {
    PatrolContext *ctx = userdata;
    AiTransform   *transform = components[0];
    AiPatrol      *patrol = components[1];

    vec3 target = {patrol->waypoints[patrol->current_index][0],
                    patrol->waypoints[patrol->current_index][1],
                    patrol->waypoints[patrol->current_index][2]};
    vec3 to_target;
    glm_vec3_sub(target, transform->position, to_target);
    to_target[1] = 0.0f;
    const float distance = glm_vec3_norm(to_target);

    if (distance < 0.3f) {
        patrol->current_index = (patrol->current_index + 1) % patrol->waypoint_count;
    } else {
        glm_vec3_normalize(to_target);
        transform->yaw_radians = atan2f(to_target[0], to_target[2]);
        vec3 step;
        glm_vec3_scale(to_target, patrol->speed_units_per_sec * ctx->delta_seconds, step);
        glm_vec3_add(transform->position, step, transform->position);
    }

    for (uint32_t i = 0; i < ctx->squad->count; ++i) {
        if (ctx->squad->entities[i] != entity) {
            continue;
        }
        versor rotation;
        glm_quatv(rotation, transform->yaw_radians, (vec3){0.0f, 1.0f, 0.0f});
        vec3 position = {transform->position[0], transform->position[1], transform->position[2]};
        rigid_body_set_transform(ctx->squad->physics_world, ctx->squad->bodies[i], position, rotation);
        break;
    }
}

static void patrol_system(World *world, float delta_time, void *userdata) {
    AiSoldierSquad *squad = userdata;
    const EcsQuery   query = {.types = {squad->transform_type, squad->patrol_type}, .type_count = 2};
    PatrolContext    ctx = {.squad = squad, .delta_seconds = delta_time};
    ecs_query_each(world, &query, patrol_query_callback, &ctx);
}

static void spawn_soldier(AiSoldierSquad *squad, const vec3 waypoints[AI_SOLDIER_MAX_WAYPOINTS],
                           uint32_t waypoint_count, float speed) {
    if (squad->count >= AI_SOLDIER_COUNT) {
        return;
    }
    const uint32_t index = squad->count;

    AiTransform transform = {0};
    transform.position[0] = waypoints[0][0];
    transform.position[1] = waypoints[0][1];
    transform.position[2] = waypoints[0][2];

    AiPatrol patrol = {0};
    for (uint32_t i = 0; i < waypoint_count; ++i) {
        patrol.waypoints[i][0] = waypoints[i][0];
        patrol.waypoints[i][1] = waypoints[i][1];
        patrol.waypoints[i][2] = waypoints[i][2];
    }
    patrol.waypoint_count = waypoint_count;
    patrol.current_index = 1 % waypoint_count;
    patrol.speed_units_per_sec = speed;

    Entity entity = ECS_INVALID_ENTITY;
    entity_create(squad->ecs_world, &entity);
    ecs_add_component(squad->ecs_world, entity, squad->transform_type, &transform);
    ecs_add_component(squad->ecs_world, entity, squad->patrol_type, &patrol);

    RigidBodyDesc body_desc = rigid_body_desc_default();
    body_desc.type = BODY_TYPE_KINEMATIC;
    body_desc.shape = shape_capsule(0.35f, 0.55f);
    body_desc.position[0] = transform.position[0];
    body_desc.position[1] = transform.position[1];
    body_desc.position[2] = transform.position[2];
    body_desc.layer = 1u;
    body_desc.layer_mask = COLLISION_LAYER_ALL;
    BodyId body = PHYSICS_INVALID_BODY_ID;
    rigid_body_create(squad->physics_world, &body_desc, &body);

    squad->entities[index] = entity;
    squad->bodies[index] = body;
    squad->count += 1;
}

Result ai_soldier_squad_create(ShaderProgram *lit_shader, PhysicsWorld *physics_world,
                                AiSoldierSquad *out_squad) {
    if (lit_shader == nullptr || physics_world == nullptr || out_squad == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    *out_squad = (AiSoldierSquad){0};
    out_squad->physics_world = physics_world;

    WorldDesc world_desc = world_desc_default();
    world_desc.max_entities = 32u;
    Result result = world_create(&world_desc, &out_squad->ecs_world);
    if (result != RESULT_OK) {
        return result;
    }
    world_register_component_type(out_squad->ecs_world, sizeof(AiTransform), &out_squad->transform_type);
    world_register_component_type(out_squad->ecs_world, sizeof(AiPatrol), &out_squad->patrol_type);

    if (primitive_create_box((vec3){0.0f, 0.0f, 0.0f}, (vec3){0.35f, 0.9f, 0.35f}, 1.0f,
                              &out_squad->mesh) != RESULT_OK) {
        world_destroy(out_squad->ecs_world);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    MaterialParams params = material_params_default();
    params.base_color[0] = 0.28f;
    params.base_color[1] = 0.32f;
    params.base_color[2] = 0.22f;
    const MaterialDesc material_desc = {.shader = lit_shader, .textures = {0}, .params = params};
    material_create(&material_desc, &out_squad->material);

    /* Four short patrol loops, one per soldier, spread through the
     * outpost rather than pathfinding between arbitrary points (out of
     * scope - see the plan this was built from). */
    const vec3 near_entrance[AI_SOLDIER_MAX_WAYPOINTS] = {
        {-3.0f, 0.9f, -14.0f}, {3.0f, 0.9f, -14.0f}, {3.0f, 0.9f, -10.0f}, {-3.0f, 0.9f, -10.0f}};
    const vec3 near_command_building[AI_SOLDIER_MAX_WAYPOINTS] = {
        {-14.0f, 0.9f, 4.0f}, {-14.0f, 0.9f, 12.0f}, {-4.0f, 0.9f, 12.0f}, {-4.0f, 0.9f, 4.0f}};
    const vec3 near_supply_shed[AI_SOLDIER_MAX_WAYPOINTS] = {
        {4.0f, 0.9f, 3.0f}, {12.0f, 0.9f, 3.0f}, {12.0f, 0.9f, 9.0f}, {4.0f, 0.9f, 9.0f}};
    const vec3 east_perimeter[AI_SOLDIER_MAX_WAYPOINTS] = {
        {16.0f, 0.9f, -15.0f}, {16.0f, 0.9f, 15.0f}, {12.0f, 0.9f, 15.0f}, {12.0f, 0.9f, -15.0f}};

    spawn_soldier(out_squad, near_entrance, 4u, 1.4f);
    spawn_soldier(out_squad, near_command_building, 4u, 1.2f);
    spawn_soldier(out_squad, near_supply_shed, 4u, 1.2f);
    spawn_soldier(out_squad, east_perimeter, 4u, 1.8f);

    const EcsSystemDesc system_desc = {.name = "ai_patrol", .fn = patrol_system, .userdata = out_squad};
    world_register_system(out_squad->ecs_world, &system_desc);

    return RESULT_OK;
}

void ai_soldier_squad_destroy(AiSoldierSquad *squad) {
    if (squad == nullptr) {
        return;
    }
    for (uint32_t i = 0; i < squad->count; ++i) {
        rigid_body_destroy(squad->physics_world, squad->bodies[i]);
    }
    material_release(squad->material);
    mesh_destroy(squad->mesh);
    world_destroy(squad->ecs_world);
}

void ai_soldier_squad_update(AiSoldierSquad *squad, float delta_seconds) {
    world_update(squad->ecs_world, delta_seconds);
}

void ai_soldier_get_world_matrix(const AiSoldierSquad *squad, uint32_t index, mat4 out_matrix) {
    AiTransform *transform =
        ecs_get_component(squad->ecs_world, squad->entities[index], squad->transform_type);
    glm_mat4_identity(out_matrix);
    if (transform == nullptr) {
        return;
    }
    vec3 position = {transform->position[0], transform->position[1], transform->position[2]};
    glm_translate(out_matrix, position);
    glm_rotate(out_matrix, transform->yaw_radians, (vec3){0.0f, 1.0f, 0.0f});
}
