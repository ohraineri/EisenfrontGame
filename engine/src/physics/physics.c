#include "eisenfront/physics.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PHYSICS_MAX_OVERLAP_PAIRS 256u
#define PHYSICS_POSITIONAL_CORRECTION_PERCENT 0.8f
#define PHYSICS_POSITIONAL_CORRECTION_SLOP 0.01f

/* ==================================================================== *
 * Shapes
 * ==================================================================== */

Shape shape_sphere(float radius) {
    return (Shape){.type = SHAPE_TYPE_SPHERE, .radius = radius};
}

Shape shape_box(vec3 half_extents) {
    Shape shape = {.type = SHAPE_TYPE_BOX};
    shape.half_extents[0] = half_extents[0];
    shape.half_extents[1] = half_extents[1];
    shape.half_extents[2] = half_extents[2];
    return shape;
}

Shape shape_capsule(float radius, float half_height) {
    return (Shape){.type = SHAPE_TYPE_CAPSULE, .radius = radius, .half_height = half_height};
}

/* ==================================================================== *
 * Geometry helpers
 * ==================================================================== */

typedef struct CollisionResult {
    bool  overlapping;
    vec3  normal;      /* points from A toward B */
    float penetration; /* > 0 when overlapping */
    vec3  point;
} CollisionResult;

static void closest_point_on_aabb(vec3 point, vec3 box_center, vec3 half_extents, vec3 out) {
    for (int i = 0; i < 3; ++i) {
        const float lo = box_center[i] - half_extents[i];
        const float hi = box_center[i] + half_extents[i];
        const float v = point[i];
        out[i] = v < lo ? lo : (v > hi ? hi : v);
    }
}

static void closest_point_on_segment(vec3 p, vec3 a, vec3 b, vec3 out) {
    vec3 ab;
    glm_vec3_sub(b, a, ab);
    vec3 ap;
    glm_vec3_sub(p, a, ap);
    const float len_sq = glm_vec3_dot(ab, ab);
    float       t = (len_sq > 0.0001f) ? glm_vec3_dot(ap, ab) / len_sq : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    vec3 scaled;
    glm_vec3_scale(ab, t, scaled);
    glm_vec3_add(a, scaled, out);
}

static CollisionResult test_sphere_sphere(vec3 pos_a, float radius_a, vec3 pos_b, float radius_b) {
    CollisionResult result = {0};
    vec3            delta;
    glm_vec3_sub(pos_b, pos_a, delta);
    const float distance = glm_vec3_norm(delta);
    const float radius_sum = radius_a + radius_b;
    if (distance >= radius_sum) {
        return result;
    }

    result.overlapping = true;
    result.penetration = radius_sum - distance;
    if (distance > 0.0001f) {
        glm_vec3_scale(delta, 1.0f / distance, result.normal);
    } else {
        result.normal[1] = 1.0f;
    }
    vec3 offset;
    glm_vec3_scale(result.normal, radius_a, offset);
    glm_vec3_add(pos_a, offset, result.point);
    return result;
}

static CollisionResult test_sphere_aabb(vec3 sphere_pos, float radius, vec3 box_pos,
                                         vec3 half_extents) {
    CollisionResult result = {0};
    vec3            closest;
    closest_point_on_aabb(sphere_pos, box_pos, half_extents, closest);

    vec3        delta;
    glm_vec3_sub(closest, sphere_pos, delta); /* points from sphere(A) toward box(B) */
    const float distance = glm_vec3_norm(delta);
    if (distance >= radius) {
        return result;
    }

    result.overlapping = true;
    result.penetration = radius - distance;
    if (distance > 0.0001f) {
        glm_vec3_scale(delta, 1.0f / distance, result.normal);
    } else {
        result.normal[1] = 1.0f;
    }
    glm_vec3_copy(closest, result.point);
    return result;
}

static CollisionResult test_aabb_aabb(vec3 pos_a, vec3 half_a, vec3 pos_b, vec3 half_b) {
    CollisionResult result = {0};
    vec3            delta;
    glm_vec3_sub(pos_b, pos_a, delta);

    float overlap[3];
    for (int i = 0; i < 3; ++i) {
        overlap[i] = (half_a[i] + half_b[i]) - fabsf(delta[i]);
        if (overlap[i] <= 0.0f) {
            return result;
        }
    }

    int   min_axis = 0;
    float min_overlap = overlap[0];
    for (int i = 1; i < 3; ++i) {
        if (overlap[i] < min_overlap) {
            min_axis = i;
            min_overlap = overlap[i];
        }
    }

    result.overlapping = true;
    result.penetration = min_overlap;
    result.normal[0] = result.normal[1] = result.normal[2] = 0.0f;
    result.normal[min_axis] = (delta[min_axis] >= 0.0f) ? 1.0f : -1.0f;
    for (int i = 0; i < 3; ++i) {
        result.point[i] = (pos_a[i] + pos_b[i]) * 0.5f;
    }
    return result;
}

static CollisionResult test_collision(const Shape *shape_a, vec3 pos_a, const Shape *shape_b,
                                       vec3 pos_b) {
    if (shape_a->type == SHAPE_TYPE_SPHERE && shape_b->type == SHAPE_TYPE_SPHERE) {
        return test_sphere_sphere(pos_a, shape_a->radius, pos_b, shape_b->radius);
    }
    if (shape_a->type == SHAPE_TYPE_SPHERE && shape_b->type == SHAPE_TYPE_BOX) {
        return test_sphere_aabb(pos_a, shape_a->radius, pos_b, shape_b->half_extents);
    }
    if (shape_a->type == SHAPE_TYPE_BOX && shape_b->type == SHAPE_TYPE_SPHERE) {
        CollisionResult result = test_sphere_aabb(pos_b, shape_b->radius, pos_a, shape_a->half_extents);
        if (result.overlapping) {
            glm_vec3_negate(result.normal); /* flip: that call computed B(sphere)->A(box) */
        }
        return result;
    }
    if (shape_a->type == SHAPE_TYPE_BOX && shape_b->type == SHAPE_TYPE_BOX) {
        return test_aabb_aabb(pos_a, shape_a->half_extents, pos_b, shape_b->half_extents);
    }
    /* Any pair involving a capsule falls back to the bounding-sphere
     * approximation for whichever side is a capsule. */
    const float radius_a =
        (shape_a->type == SHAPE_TYPE_CAPSULE) ? shape_a->radius + shape_a->half_height
        : (shape_a->type == SHAPE_TYPE_SPHERE)  ? shape_a->radius
                                                  : glm_vec3_norm(shape_a->half_extents);
    const float radius_b =
        (shape_b->type == SHAPE_TYPE_CAPSULE) ? shape_b->radius + shape_b->half_height
        : (shape_b->type == SHAPE_TYPE_SPHERE)  ? shape_b->radius
                                                  : glm_vec3_norm(shape_b->half_extents);
    return test_sphere_sphere(pos_a, radius_a, pos_b, radius_b);
}

/* ==================================================================== *
 * World / bodies
 * ==================================================================== */

#define PHYSICS_BODY_INDEX_BITS 20u
#define PHYSICS_BODY_INDEX_MASK ((1u << PHYSICS_BODY_INDEX_BITS) - 1u)
#define PHYSICS_MAX_GENERATION ((1u << (32u - PHYSICS_BODY_INDEX_BITS)) - 1u)

typedef struct Body {
    uint32_t       generation;
    bool           active;
    BodyType       type;
    Shape          shape;
    vec3           position;
    versor         rotation;
    vec3           linear_velocity;
    vec3           force_accum;
    float          mass;
    float          inv_mass;
    float          restitution;
    float          friction;
    float          linear_damping;
    CollisionLayer layer;
    CollisionLayer layer_mask;
    bool           is_trigger;
} Body;

struct PhysicsWorld {
    Body    *bodies;
    uint32_t max_bodies;
    uint32_t *free_indices;
    uint32_t free_count;
    vec3     gravity;

    TriggerEventFn trigger_callback;
    void          *trigger_userdata;

    BodyId  *prev_overlap_pairs; /* [a0, b0, a1, b1, ...] */
    uint32_t prev_overlap_count;
    BodyId  *curr_overlap_pairs;
    uint32_t curr_overlap_count;
};

static uint32_t body_index_of(BodyId id) {
    return id & PHYSICS_BODY_INDEX_MASK;
}
static uint32_t body_generation_of(BodyId id) {
    return id >> PHYSICS_BODY_INDEX_BITS;
}
static BodyId make_body_id(uint32_t index, uint32_t generation) {
    return (BodyId)((generation << PHYSICS_BODY_INDEX_BITS) | (index & PHYSICS_BODY_INDEX_MASK));
}

RigidBodyDesc rigid_body_desc_default(void) {
    RigidBodyDesc desc;
    desc.type = BODY_TYPE_DYNAMIC;
    desc.shape = shape_sphere(0.5f);
    desc.position[0] = desc.position[1] = desc.position[2] = 0.0f;
    glm_quat_identity(desc.rotation);
    desc.mass = 1.0f;
    desc.restitution = 0.2f;
    desc.friction = 0.5f;
    desc.linear_damping = 0.01f;
    desc.layer = 1u;
    desc.layer_mask = COLLISION_LAYER_ALL;
    desc.is_trigger = false;
    return desc;
}

PhysicsWorldDesc physics_world_desc_default(void) {
    PhysicsWorldDesc desc;
    desc.max_bodies = 1024;
    desc.gravity[0] = 0.0f;
    desc.gravity[1] = -9.81f;
    desc.gravity[2] = 0.0f;
    return desc;
}

Result physics_world_create(const PhysicsWorldDesc *desc, PhysicsWorld **out_world) {
    if (out_world == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    const PhysicsWorldDesc effective = (desc != nullptr) ? *desc : physics_world_desc_default();
    if (effective.max_bodies == 0 || effective.max_bodies > PHYSICS_BODY_INDEX_MASK) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    PhysicsWorld *world = calloc(1, sizeof(*world));
    if (world == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    world->bodies = calloc((size_t)effective.max_bodies + 1, sizeof(Body));
    world->free_indices = malloc(sizeof(uint32_t) * effective.max_bodies);
    world->prev_overlap_pairs = malloc(sizeof(BodyId) * 2 * PHYSICS_MAX_OVERLAP_PAIRS);
    world->curr_overlap_pairs = malloc(sizeof(BodyId) * 2 * PHYSICS_MAX_OVERLAP_PAIRS);
    if (world->bodies == nullptr || world->free_indices == nullptr ||
        world->prev_overlap_pairs == nullptr || world->curr_overlap_pairs == nullptr) {
        free(world->bodies);
        free(world->free_indices);
        free(world->prev_overlap_pairs);
        free(world->curr_overlap_pairs);
        free(world);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    for (uint32_t i = 0; i < effective.max_bodies; ++i) {
        world->free_indices[i] = effective.max_bodies - i;
    }
    world->free_count = effective.max_bodies;
    world->max_bodies = effective.max_bodies;
    glm_vec3_copy(effective.gravity, world->gravity);
    world->prev_overlap_count = 0;
    world->curr_overlap_count = 0;

    *out_world = world;
    return RESULT_OK;
}

void physics_world_destroy(PhysicsWorld *world) {
    if (world == nullptr) {
        return;
    }
    free(world->bodies);
    free(world->free_indices);
    free(world->prev_overlap_pairs);
    free(world->curr_overlap_pairs);
    free(world);
}

Result rigid_body_create(PhysicsWorld *world, const RigidBodyDesc *desc, BodyId *out_body) {
    if (world == nullptr || desc == nullptr || out_body == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (world->free_count == 0) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }

    const uint32_t index = world->free_indices[--world->free_count];
    Body          *body = &world->bodies[index];

    body->active = true;
    body->type = desc->type;
    body->shape = desc->shape;
    body->position[0] = desc->position[0];
    body->position[1] = desc->position[1];
    body->position[2] = desc->position[2];
    body->rotation[0] = desc->rotation[0];
    body->rotation[1] = desc->rotation[1];
    body->rotation[2] = desc->rotation[2];
    body->rotation[3] = desc->rotation[3];
    body->linear_velocity[0] = body->linear_velocity[1] = body->linear_velocity[2] = 0.0f;
    body->force_accum[0] = body->force_accum[1] = body->force_accum[2] = 0.0f;
    body->mass = (desc->type == BODY_TYPE_DYNAMIC && desc->mass > 0.0f) ? desc->mass : 0.0f;
    body->inv_mass = (body->mass > 0.0f) ? (1.0f / body->mass) : 0.0f;
    body->restitution = desc->restitution;
    body->friction = desc->friction;
    body->linear_damping = desc->linear_damping;
    body->layer = desc->layer;
    body->layer_mask = desc->layer_mask;
    body->is_trigger = desc->is_trigger;

    *out_body = make_body_id(index, body->generation);
    return RESULT_OK;
}

bool rigid_body_is_valid(const PhysicsWorld *world, BodyId body) {
    ASSERT(world != nullptr);
    if (world == nullptr || body == PHYSICS_INVALID_BODY_ID) {
        return false;
    }
    const uint32_t index = body_index_of(body);
    if (index == 0 || index > world->max_bodies) {
        return false;
    }
    return world->bodies[index].active &&
           world->bodies[index].generation == body_generation_of(body);
}

Result rigid_body_destroy(PhysicsWorld *world, BodyId body) {
    if (!rigid_body_is_valid(world, body)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    const uint32_t index = body_index_of(body);
    world->bodies[index].active = false;
    world->bodies[index].generation =
        (world->bodies[index].generation < PHYSICS_MAX_GENERATION)
            ? world->bodies[index].generation + 1
            : 0;
    world->free_indices[world->free_count++] = index;
    return RESULT_OK;
}

Result rigid_body_get_position(const PhysicsWorld *world, BodyId body, vec3 out_position) {
    if (!rigid_body_is_valid(world, body)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    glm_vec3_copy(world->bodies[body_index_of(body)].position, out_position);
    return RESULT_OK;
}

Result rigid_body_get_rotation(const PhysicsWorld *world, BodyId body, versor out_rotation) {
    if (!rigid_body_is_valid(world, body)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    glm_quat_copy(world->bodies[body_index_of(body)].rotation, out_rotation);
    return RESULT_OK;
}

Result rigid_body_set_transform(PhysicsWorld *world, BodyId body, vec3 position,
                                 versor rotation) {
    if (!rigid_body_is_valid(world, body)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    Body *b = &world->bodies[body_index_of(body)];
    glm_vec3_copy(position, b->position);
    glm_quat_copy(rotation, b->rotation);
    return RESULT_OK;
}

Result rigid_body_get_linear_velocity(const PhysicsWorld *world, BodyId body, vec3 out_velocity) {
    if (!rigid_body_is_valid(world, body)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    glm_vec3_copy(world->bodies[body_index_of(body)].linear_velocity, out_velocity);
    return RESULT_OK;
}

Result rigid_body_set_linear_velocity(PhysicsWorld *world, BodyId body, vec3 velocity) {
    if (!rigid_body_is_valid(world, body)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    glm_vec3_copy(velocity, world->bodies[body_index_of(body)].linear_velocity);
    return RESULT_OK;
}

Result rigid_body_apply_force(PhysicsWorld *world, BodyId body, vec3 force) {
    if (!rigid_body_is_valid(world, body)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    Body *b = &world->bodies[body_index_of(body)];
    glm_vec3_add(b->force_accum, force, b->force_accum);
    return RESULT_OK;
}

Result rigid_body_apply_impulse(PhysicsWorld *world, BodyId body, vec3 impulse) {
    if (!rigid_body_is_valid(world, body)) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    Body *b = &world->bodies[body_index_of(body)];
    if (b->inv_mass <= 0.0f) {
        return RESULT_OK;
    }
    vec3 delta_v;
    glm_vec3_scale(impulse, b->inv_mass, delta_v);
    glm_vec3_add(b->linear_velocity, delta_v, b->linear_velocity);
    return RESULT_OK;
}

void physics_world_set_trigger_callback(PhysicsWorld *world, TriggerEventFn fn, void *userdata) {
    ASSERT(world != nullptr);
    if (world == nullptr) {
        return;
    }
    world->trigger_callback = fn;
    world->trigger_userdata = userdata;
}

static bool layers_collide(const Body *a, const Body *b) {
    return (a->layer & b->layer_mask) != 0 && (b->layer & a->layer_mask) != 0;
}

static void resolve_collision(Body *a, Body *b, const CollisionResult *hit) {
    const float total_inv_mass = a->inv_mass + b->inv_mass;
    if (total_inv_mass <= 0.0f) {
        return;
    }

    const float correction_mag =
        fmaxf(hit->penetration - PHYSICS_POSITIONAL_CORRECTION_SLOP, 0.0f) / total_inv_mass *
        PHYSICS_POSITIONAL_CORRECTION_PERCENT;
    vec3 correction;
    glm_vec3_scale(hit->normal, correction_mag, correction);
    vec3 scaled;
    glm_vec3_scale(correction, -a->inv_mass, scaled);
    glm_vec3_add(a->position, scaled, a->position);
    glm_vec3_scale(correction, b->inv_mass, scaled);
    glm_vec3_add(b->position, scaled, b->position);

    vec3 relative_velocity;
    glm_vec3_sub(b->linear_velocity, a->linear_velocity, relative_velocity);
    const float velocity_along_normal = glm_vec3_dot(relative_velocity, hit->normal);
    if (velocity_along_normal > 0.0f) {
        return; /* already separating */
    }

    const float restitution = fminf(a->restitution, b->restitution);
    const float impulse_mag = -(1.0f + restitution) * velocity_along_normal / total_inv_mass;
    vec3        impulse;
    glm_vec3_scale(hit->normal, impulse_mag, impulse);
    glm_vec3_scale(impulse, -a->inv_mass, scaled);
    glm_vec3_add(a->linear_velocity, scaled, a->linear_velocity);
    glm_vec3_scale(impulse, b->inv_mass, scaled);
    glm_vec3_add(b->linear_velocity, scaled, b->linear_velocity);

    /* Simplified Coulomb friction: damp the tangential component of the
     * relative velocity, clamped to the normal impulse magnitude scaled
     * by the combined friction coefficient. */
    vec3 normal_component;
    glm_vec3_scale(hit->normal, velocity_along_normal, normal_component);
    vec3 tangent_velocity;
    glm_vec3_sub(relative_velocity, normal_component, tangent_velocity);
    const float tangent_speed = glm_vec3_norm(tangent_velocity);
    if (tangent_speed > 0.0001f) {
        vec3 tangent_dir;
        glm_vec3_scale(tangent_velocity, 1.0f / tangent_speed, tangent_dir);
        const float friction_coefficient = sqrtf(a->friction * b->friction);
        const float friction_impulse_mag =
            fminf(tangent_speed / total_inv_mass, fabsf(impulse_mag) * friction_coefficient);
        vec3 friction_impulse;
        glm_vec3_scale(tangent_dir, friction_impulse_mag, friction_impulse);
        glm_vec3_scale(friction_impulse, -a->inv_mass, scaled);
        glm_vec3_add(a->linear_velocity, scaled, a->linear_velocity);
        glm_vec3_scale(friction_impulse, b->inv_mass, scaled);
        glm_vec3_add(b->linear_velocity, scaled, b->linear_velocity);
    }
}

static bool find_pair(const BodyId *pairs, uint32_t count, BodyId a, BodyId b) {
    for (uint32_t i = 0; i < count; ++i) {
        if (pairs[i * 2] == a && pairs[i * 2 + 1] == b) {
            return true;
        }
    }
    return false;
}

void physics_world_step(PhysicsWorld *world, float delta_time) {
    ASSERT(world != nullptr);
    if (world == nullptr || delta_time <= 0.0f) {
        return;
    }

    for (uint32_t i = 1; i <= world->max_bodies; ++i) {
        Body *body = &world->bodies[i];
        if (!body->active) {
            continue;
        }
        if (body->type == BODY_TYPE_DYNAMIC) {
            vec3 acceleration;
            glm_vec3_scale(world->gravity, 1.0f, acceleration);
            vec3 force_accel;
            glm_vec3_scale(body->force_accum, body->inv_mass, force_accel);
            glm_vec3_add(acceleration, force_accel, acceleration);
            vec3 delta_v;
            glm_vec3_scale(acceleration, delta_time, delta_v);
            glm_vec3_add(body->linear_velocity, delta_v, body->linear_velocity);

            const float damping = fmaxf(0.0f, 1.0f - body->linear_damping * delta_time);
            glm_vec3_scale(body->linear_velocity, damping, body->linear_velocity);

            body->force_accum[0] = body->force_accum[1] = body->force_accum[2] = 0.0f;
        }
        if (body->type == BODY_TYPE_DYNAMIC || body->type == BODY_TYPE_KINEMATIC) {
            vec3 delta_p;
            glm_vec3_scale(body->linear_velocity, delta_time, delta_p);
            glm_vec3_add(body->position, delta_p, body->position);
        }
    }

    world->curr_overlap_count = 0;

    for (uint32_t i = 1; i <= world->max_bodies; ++i) {
        Body *a = &world->bodies[i];
        if (!a->active) {
            continue;
        }
        for (uint32_t j = i + 1; j <= world->max_bodies; ++j) {
            Body *b = &world->bodies[j];
            if (!b->active) {
                continue;
            }
            if (a->type == BODY_TYPE_STATIC && b->type == BODY_TYPE_STATIC) {
                continue;
            }
            if (!layers_collide(a, b)) {
                continue;
            }

            const CollisionResult hit = test_collision(&a->shape, a->position, &b->shape, b->position);
            if (!hit.overlapping) {
                continue;
            }

            if (a->is_trigger || b->is_trigger) {
                if (world->curr_overlap_count < PHYSICS_MAX_OVERLAP_PAIRS) {
                    const BodyId id_a = make_body_id(i, a->generation);
                    const BodyId id_b = make_body_id(j, b->generation);
                    world->curr_overlap_pairs[world->curr_overlap_count * 2] = id_a;
                    world->curr_overlap_pairs[world->curr_overlap_count * 2 + 1] = id_b;
                    world->curr_overlap_count += 1;
                }
            } else {
                resolve_collision(a, b, &hit);
            }
        }
    }

    if (world->trigger_callback != nullptr) {
        for (uint32_t i = 0; i < world->curr_overlap_count; ++i) {
            const BodyId a = world->curr_overlap_pairs[i * 2];
            const BodyId b = world->curr_overlap_pairs[i * 2 + 1];
            if (!find_pair(world->prev_overlap_pairs, world->prev_overlap_count, a, b)) {
                const bool a_is_trigger = world->bodies[body_index_of(a)].is_trigger;
                world->trigger_callback(a_is_trigger ? a : b, a_is_trigger ? b : a, true,
                                         world->trigger_userdata);
            }
        }
        for (uint32_t i = 0; i < world->prev_overlap_count; ++i) {
            const BodyId a = world->prev_overlap_pairs[i * 2];
            const BodyId b = world->prev_overlap_pairs[i * 2 + 1];
            if (!find_pair(world->curr_overlap_pairs, world->curr_overlap_count, a, b)) {
                const bool a_is_trigger =
                    rigid_body_is_valid(world, a) && world->bodies[body_index_of(a)].is_trigger;
                world->trigger_callback(a_is_trigger ? a : b, a_is_trigger ? b : a, false,
                                         world->trigger_userdata);
            }
        }
    }

    memcpy(world->prev_overlap_pairs, world->curr_overlap_pairs,
           sizeof(BodyId) * 2 * world->curr_overlap_count);
    world->prev_overlap_count = world->curr_overlap_count;
}

/* ==================================================================== *
 * Raycasts
 * ==================================================================== */

static bool ray_sphere(vec3 origin, vec3 direction, vec3 center, float radius, float max_distance,
                        float *out_t) {
    vec3 oc;
    glm_vec3_sub(origin, center, oc);
    const float b = glm_vec3_dot(oc, direction);
    const float c = glm_vec3_dot(oc, oc) - radius * radius;
    const float discriminant = b * b - c;
    if (discriminant < 0.0f) {
        return false;
    }
    const float sqrt_disc = sqrtf(discriminant);
    float       t = -b - sqrt_disc;
    if (t < 0.0f) {
        t = -b + sqrt_disc;
    }
    if (t < 0.0f || t > max_distance) {
        return false;
    }
    *out_t = t;
    return true;
}

static bool ray_aabb(vec3 origin, vec3 direction, vec3 box_pos, vec3 half_extents,
                      float max_distance, float *out_t) {
    float tmin = 0.0f;
    float tmax = max_distance;
    for (int i = 0; i < 3; ++i) {
        const float lo = box_pos[i] - half_extents[i];
        const float hi = box_pos[i] + half_extents[i];
        if (fabsf(direction[i]) < 1e-8f) {
            if (origin[i] < lo || origin[i] > hi) {
                return false;
            }
            continue;
        }
        const float inv_d = 1.0f / direction[i];
        float       t1 = (lo - origin[i]) * inv_d;
        float       t2 = (hi - origin[i]) * inv_d;
        if (t1 > t2) {
            const float tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        tmin = fmaxf(tmin, t1);
        tmax = fminf(tmax, t2);
        if (tmin > tmax) {
            return false;
        }
    }
    *out_t = tmin;
    return true;
}

bool physics_raycast(const PhysicsWorld *world, vec3 origin, vec3 direction, float max_distance,
                      CollisionLayer layer_mask, RaycastHit *out_hit) {
    ASSERT(world != nullptr);
    if (world == nullptr || out_hit == nullptr) {
        return false;
    }

    bool  found = false;
    float best_t = max_distance;
    for (uint32_t i = 1; i <= world->max_bodies; ++i) {
        const Body *body = &world->bodies[i];
        if (!body->active || (body->layer & layer_mask) == 0) {
            continue;
        }

        float t = 0.0f;
        bool  hit = false;
        vec3  normal = {0.0f, 0.0f, 0.0f};
        if (body->shape.type == SHAPE_TYPE_SPHERE || body->shape.type == SHAPE_TYPE_CAPSULE) {
            const float radius = (body->shape.type == SHAPE_TYPE_SPHERE)
                                      ? body->shape.radius
                                      : body->shape.radius + body->shape.half_height;
            hit = ray_sphere(origin, direction, body->position, radius, best_t, &t);
            if (hit) {
                vec3 point;
                glm_vec3_scale(direction, t, point);
                glm_vec3_add(origin, point, point);
                glm_vec3_sub(point, body->position, normal);
                glm_vec3_normalize(normal);
            }
        } else {
            hit = ray_aabb(origin, direction, body->position, body->shape.half_extents, best_t, &t);
            if (hit) {
                vec3 point;
                glm_vec3_scale(direction, t, point);
                glm_vec3_add(origin, point, point);
                vec3 local;
                glm_vec3_sub(point, body->position, local);
                int   max_axis = 0;
                float max_component = fabsf(local[0]) / body->shape.half_extents[0];
                for (int axis = 1; axis < 3; ++axis) {
                    const float component = fabsf(local[axis]) / body->shape.half_extents[axis];
                    if (component > max_component) {
                        max_axis = axis;
                        max_component = component;
                    }
                }
                normal[max_axis] = local[max_axis] >= 0.0f ? 1.0f : -1.0f;
            }
        }

        if (hit && t <= best_t) {
            found = true;
            best_t = t;
            out_hit->body = make_body_id(i, body->generation);
            out_hit->distance = t;
            glm_vec3_scale(direction, t, out_hit->point);
            glm_vec3_add(origin, out_hit->point, out_hit->point);
            glm_vec3_copy(normal, out_hit->normal);
        }
    }

    return found;
}

/* ==================================================================== *
 * Character controller
 * ==================================================================== */

struct CharacterController {
    PhysicsWorld  *world;
    float          radius;
    float          half_height;
    vec3           position;
    CollisionLayer layer;
    CollisionLayer layer_mask;
    bool           grounded;
};

Result character_controller_create(PhysicsWorld *world, const CharacterControllerDesc *desc,
                                    CharacterController **out_controller) {
    if (world == nullptr || desc == nullptr || out_controller == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    CharacterController *controller = malloc(sizeof(*controller));
    if (controller == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    controller->world = world;
    controller->radius = desc->radius;
    controller->half_height = desc->half_height;
    glm_vec3_copy(desc->position, controller->position);
    controller->layer = desc->layer;
    controller->layer_mask = desc->layer_mask;
    controller->grounded = false;

    *out_controller = controller;
    return RESULT_OK;
}

void character_controller_destroy(CharacterController *controller) {
    free(controller);
}

/* Iterative alternating-projection closest point between a segment and
 * an AABB: project the current guess onto the box, then the box point
 * back onto the segment, repeat. Converges quickly for this kind of
 * simple convex-vs-segment case. */
static void closest_segment_to_aabb(vec3 seg_a, vec3 seg_b, vec3 box_pos, vec3 half_extents,
                                     vec3 out_segment_point, vec3 out_box_point) {
    vec3 point_on_segment;
    glm_vec3_copy(seg_a, point_on_segment);
    vec3 point_on_box = {0.0f, 0.0f, 0.0f};
    for (int iter = 0; iter < 3; ++iter) {
        closest_point_on_aabb(point_on_segment, box_pos, half_extents, point_on_box);
        closest_point_on_segment(point_on_box, seg_a, seg_b, point_on_segment);
    }
    glm_vec3_copy(point_on_segment, out_segment_point);
    glm_vec3_copy(point_on_box, out_box_point);
}

static CollisionResult capsule_vs_body(vec3 capsule_pos, float radius, float half_height,
                                        const Body *body) {
    vec3 seg_a = {capsule_pos[0], capsule_pos[1] - half_height, capsule_pos[2]};
    vec3 seg_b = {capsule_pos[0], capsule_pos[1] + half_height, capsule_pos[2]};

    if (body->shape.type == SHAPE_TYPE_BOX) {
        vec3 segment_point, box_point;
        closest_segment_to_aabb(seg_a, seg_b, body->position, body->shape.half_extents,
                                 segment_point, box_point);
        return test_sphere_sphere(segment_point, radius, box_point, 0.0f);
    }

    /* SPHERE or CAPSULE: treat the other body as a sphere (a capsule
     * body already collapses to a bounding sphere elsewhere in this
     * module, per the file header comment). */
    const float other_radius =
        (body->shape.type == SHAPE_TYPE_CAPSULE) ? body->shape.radius + body->shape.half_height
                                                    : body->shape.radius;
    vec3 closest;
    closest_point_on_segment(body->position, seg_a, seg_b, closest);
    return test_sphere_sphere(closest, radius, body->position, other_radius);
}

void character_controller_move(CharacterController *controller, vec3 desired_displacement,
                                vec3 out_position) {
    ASSERT(controller != nullptr);
    if (controller == nullptr) {
        return;
    }

    vec3 position;
    glm_vec3_copy(controller->position, position);
    vec3 remaining;
    glm_vec3_copy(desired_displacement, remaining);

    for (int iteration = 0; iteration < 4; ++iteration) {
        vec3 tentative;
        glm_vec3_add(position, remaining, tentative);

        bool penetrated = false;
        for (uint32_t i = 1; i <= controller->world->max_bodies; ++i) {
            const Body *body = &controller->world->bodies[i];
            if (!body->active || body->is_trigger) {
                continue;
            }
            if ((body->layer & controller->layer_mask) == 0 ||
                (controller->layer & body->layer_mask) == 0) {
                continue;
            }

            const CollisionResult hit =
                capsule_vs_body(tentative, controller->radius, controller->half_height, body);
            if (!hit.overlapping) {
                continue;
            }

            vec3 push;
            glm_vec3_scale(hit.normal, -hit.penetration, push);
            glm_vec3_add(tentative, push, tentative);

            /* Slide: remove the component of the remaining displacement
             * that points into the surface, so the next iteration keeps
             * moving along it instead of stopping dead. */
            const float into_surface = glm_vec3_dot(remaining, hit.normal);
            if (into_surface < 0.0f) {
                vec3 correction;
                glm_vec3_scale(hit.normal, into_surface, correction);
                glm_vec3_sub(remaining, correction, remaining);
            }
            penetrated = true;
        }

        glm_vec3_copy(tentative, position);
        if (!penetrated) {
            break;
        }
    }

    glm_vec3_copy(position, controller->position);
    glm_vec3_copy(position, out_position);

    /* Grounded probe: a short ray straight down from the capsule's
     * bottom cap. */
    controller->grounded = false;
    vec3 probe_origin = {position[0], position[1] - controller->half_height, position[2]};
    for (uint32_t i = 1; i <= controller->world->max_bodies; ++i) {
        const Body *body = &controller->world->bodies[i];
        if (!body->active || body->is_trigger) {
            continue;
        }
        float t = 0.0f;
        bool  hit = false;
        vec3  down = {0.0f, -1.0f, 0.0f};
        if (body->shape.type == SHAPE_TYPE_BOX) {
            hit = ray_aabb(probe_origin, down, body->position, body->shape.half_extents,
                            controller->radius * 0.3f, &t);
        } else {
            const float radius = (body->shape.type == SHAPE_TYPE_CAPSULE)
                                      ? body->shape.radius + body->shape.half_height
                                      : body->shape.radius;
            hit = ray_sphere(probe_origin, down, body->position, radius, controller->radius * 0.3f,
                              &t);
        }
        if (hit) {
            controller->grounded = true;
            break;
        }
    }
}

void character_controller_get_position(const CharacterController *controller, vec3 out_position) {
    ASSERT(controller != nullptr);
    if (controller == nullptr) {
        return;
    }
    glm_vec3_copy(controller->position, out_position);
}

bool character_controller_is_grounded(const CharacterController *controller) {
    ASSERT(controller != nullptr);
    return controller != nullptr && controller->grounded;
}
