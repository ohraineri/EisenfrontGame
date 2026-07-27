#include "sky.h"

#include "primitives.h"

#include <glad/glad.h>

#include <stdio.h>

Result sky_create(const char *assets_dir, vec3 horizon_color, vec3 zenith_color,
                   vec3 sun_direction, vec3 sun_color, Sky *out_sky) {
    if (assets_dir == nullptr || out_sky == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    char vertex_path[512];
    char fragment_path[512];
    snprintf(vertex_path, sizeof(vertex_path), "%s/shaders/sky.vert", assets_dir);
    snprintf(fragment_path, sizeof(fragment_path), "%s/shaders/sky.frag", assets_dir);

    const ShaderProgramDesc shader_desc = {
        .vertex_path = vertex_path,
        .fragment_path = fragment_path,
        .geometry_path = nullptr,
    };
    ShaderProgram *shader = nullptr;
    Result         result = shader_program_load(&shader_desc, &shader);
    if (result != RESULT_OK) {
        return result;
    }

    Mesh *mesh = nullptr;
    result = primitive_create_sky_dome(300.0f, 24u, 32u, &mesh);
    if (result != RESULT_OK) {
        shader_program_release(shader);
        return result;
    }

    out_sky->shader = shader;
    out_sky->mesh = mesh;
    glm_vec3_copy(horizon_color, out_sky->horizon_color);
    glm_vec3_copy(zenith_color, out_sky->zenith_color);
    glm_vec3_copy(sun_direction, out_sky->sun_direction);
    glm_vec3_copy(sun_color, out_sky->sun_color);
    return RESULT_OK;
}

void sky_destroy(Sky *sky) {
    if (sky == nullptr) {
        return;
    }
    mesh_destroy(sky->mesh);
    shader_program_release(sky->shader);
}

void sky_draw(Renderer *renderer, const Sky *sky, mat4 view, mat4 proj) {
    shader_set_uniform_mat4(sky->shader, "uView", (const float *)view);
    shader_set_uniform_mat4(sky->shader, "uProj", (const float *)proj);
    shader_set_uniform_3f(sky->shader, "uHorizonColor", sky->horizon_color[0], sky->horizon_color[1],
                            sky->horizon_color[2]);
    shader_set_uniform_3f(sky->shader, "uZenithColor", sky->zenith_color[0], sky->zenith_color[1],
                            sky->zenith_color[2]);
    shader_set_uniform_3f(sky->shader, "uSunDirection", sky->sun_direction[0], sky->sun_direction[1],
                            sky->sun_direction[2]);
    shader_set_uniform_3f(sky->shader, "uSunColor", sky->sun_color[0], sky->sun_color[1],
                            sky->sun_color[2]);

    /* Depth-trick draw (see sky.vert): must run after every opaque
     * object, with GL_LEQUAL so it only fills pixels nothing else
     * touched. Restored to the engine's normal GL_LESS afterward so it
     * doesn't leak into whatever draws next. */
    glDepthFunc(GL_LEQUAL);

    renderer_begin_frame(renderer);
    const RenderCommand command = {
        .vertex_array = mesh_get_vertex_array_handle(sky->mesh),
        .shader_program = shader_program_get_gl_handle(sky->shader),
        .topology = PRIMITIVE_TOPOLOGY_TRIANGLES,
        .indexed = mesh_is_indexed(sky->mesh),
        .index_count = mesh_get_index_count(sky->mesh),
        .vertex_count = mesh_get_vertex_count(sky->mesh),
        .instance_count = 1,
        .sort_key = 0,
    };
    renderer_submit(renderer, &command);
    renderer_end_frame(renderer);

    glDepthFunc(GL_LESS);
}
