/*
 * The outpost itself: a small defended forward position built through
 * Scene's node hierarchy (perimeter barriers, structures, props,
 * grouped under group nodes with real parent-child transform
 * composition) with a matching static Physics box body created 1:1
 * for every visual box, so the player can never walk through
 * something they can see.
 *
 * Every visual box shares the lit shader (passed in, owned by the
 * caller) across a handful of Materials (barrier/building/shed/
 * watchtower/prop) - one Mesh per object rather than merged batches:
 * simpler, and at outpost scale (a few dozen static objects) each gets
 * its own cheap flush (see main.c's file header comment on why
 * per-material-group flushing is required at all).
 */
#ifndef OUTPOST_SCENE_LEVEL_H
#define OUTPOST_SCENE_LEVEL_H

#include "eisenfront/material.h"
#include "eisenfront/mesh.h"
#include "eisenfront/physics.h"
#include "eisenfront/scene.h"
#include "eisenfront/shader.h"

#define OUTPOST_MAX_STATIC_OBJECTS 96u

typedef struct StaticObject {
    Mesh     *mesh;
    Material *material;
    BodyId    body; /* static box body matching this object's visual bounds exactly */
} StaticObject;

typedef struct OutpostLevel {
    Scene   *scene;
    Material *barrier_material;
    Material *building_material;
    Material *shed_material;
    Material *watchtower_material;
    Material *prop_material;

    StaticObject objects[OUTPOST_MAX_STATIC_OBJECTS];
    uint32_t     object_count;
} OutpostLevel;

/* lit_shader is not retained/owned - the caller's existing shader is
 * reused for every material this level creates. physics_world must
 * outlive the level (every StaticObject.body lives in it). */
Result outpost_level_create(ShaderProgram *lit_shader, PhysicsWorld *physics_world,
                             OutpostLevel *out_level);
void   outpost_level_destroy(OutpostLevel *level, PhysicsWorld *physics_world);

#endif /* OUTPOST_SCENE_LEVEL_H */
