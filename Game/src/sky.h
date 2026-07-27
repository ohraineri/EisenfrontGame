/*
 * Procedural sky dome - see the file header comments in primitives.h
 * (primitive_create_sky_dome) and Assets/shaders/sky.vert for why this
 * exists instead of the engine's own Skybox module (its only entry
 * point requires a baked cubemap file, which this slice's "generate at
 * load time, no files" decision rules out).
 */
#ifndef OUTPOST_SKY_H
#define OUTPOST_SKY_H

#include "eisenfront/mesh.h"
#include "eisenfront/renderer.h"
#include "eisenfront/shader.h"

#include <cglm/cglm.h>

typedef struct Sky {
    ShaderProgram *shader;
    Mesh          *mesh;
    vec3           horizon_color;
    vec3           zenith_color;
    vec3           sun_direction; /* points FROM the sun TOWARD the scene */
    vec3           sun_color;
} Sky;

Result sky_create(const char *assets_dir, vec3 horizon_color, vec3 zenith_color,
                   vec3 sun_direction, vec3 sun_color, Sky *out_sky);
void   sky_destroy(Sky *sky);

/* Must be drawn after every opaque object in the frame - see
 * sky.vert's file header comment on the depth trick this relies on. */
void sky_draw(Renderer *renderer, const Sky *sky, mat4 view, mat4 proj);

#endif /* OUTPOST_SKY_H */
