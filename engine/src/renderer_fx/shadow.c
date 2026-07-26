#include "eisenfront/shadow.h"

#include "renderer_fx_internal.h"

#include <math.h>
#include <stdlib.h>

static const char *const k_shadow_vertex_source =
    "#version 460 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "uniform mat4 uLightSpaceMatrix;\n"
    "uniform mat4 uModel;\n"
    "void main() { gl_Position = uLightSpaceMatrix * uModel * vec4(aPos, 1.0); }\n";

/* No color output: this pass only ever writes the depth attachment
 * (the shadow map's framebuffer has no color attachment at all - see
 * shadow_map_create), but every GL implementation still requires a
 * bound fragment shader for a complete pipeline. */
static const char *const k_shadow_fragment_source = "#version 460 core\n"
                                                      "void main() { }\n";

struct ShadowMap {
    Framebuffer *framebuffer;
    uint32_t     gl_program;
    GLint        light_space_matrix_location;
    GLint        model_location;
    mat4          light_space_matrix;
};

Result shadow_map_create(uint32_t resolution, ShadowMap **out_shadow_map) {
    if (resolution == 0 || out_shadow_map == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const FramebufferDesc fb_desc = {
        .width = resolution,
        .height = resolution,
        .sample_count = 1,
        .has_color = false,
        .has_depth = true,
    };
    Framebuffer *framebuffer = nullptr;
    Result       result = framebuffer_create(&fb_desc, &framebuffer);
    if (result != RESULT_OK) {
        return result;
    }

    const GLuint program = renderer_fx_link_program(k_shadow_vertex_source, k_shadow_fragment_source);
    if (program == 0) {
        framebuffer_destroy(framebuffer);
        return RESULT_ERROR_PLATFORM;
    }

    ShadowMap *shadow_map = malloc(sizeof(*shadow_map));
    if (shadow_map == nullptr) {
        glDeleteProgram(program);
        framebuffer_destroy(framebuffer);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    shadow_map->framebuffer = framebuffer;
    shadow_map->gl_program = program;
    shadow_map->light_space_matrix_location = glGetUniformLocation(program, "uLightSpaceMatrix");
    shadow_map->model_location = glGetUniformLocation(program, "uModel");
    glm_mat4_identity(shadow_map->light_space_matrix);

    *out_shadow_map = shadow_map;
    return RESULT_OK;
}

void shadow_map_destroy(ShadowMap *shadow_map) {
    if (shadow_map == nullptr) {
        return;
    }
    glDeleteProgram(shadow_map->gl_program);
    framebuffer_destroy(shadow_map->framebuffer);
    free(shadow_map);
}

void shadow_map_set_light(ShadowMap *shadow_map, vec3 light_direction, vec3 scene_center,
                           float scene_radius) {
    ASSERT(shadow_map != nullptr);
    if (shadow_map == nullptr) {
        return;
    }

    vec3 direction = {light_direction[0], light_direction[1], light_direction[2]};
    glm_vec3_normalize(direction);

    /* Place the light "eye" far enough back along -direction to see the
     * whole bounded sphere, then look at its center. */
    vec3 eye;
    glm_vec3_scale(direction, -scene_radius * 2.0f, eye);
    glm_vec3_add(eye, scene_center, eye);

    /* World up is unusable as the look-at up hint when the light points
     * nearly straight up or down (the cross products view construction
     * depends on degenerate near-parallel vectors); fall back to world
     * forward in that case, same robustness concern camera.c documents
     * for camera_get_right(). */
    vec3 up = {0.0f, 1.0f, 0.0f};
    if (fabsf(glm_vec3_dot(direction, up)) > 0.999f) {
        up[0] = 0.0f;
        up[1] = 0.0f;
        up[2] = 1.0f;
    }

    mat4 view;
    glm_lookat(eye, scene_center, up, view);

    mat4 projection;
    glm_ortho(-scene_radius, scene_radius, -scene_radius, scene_radius, 0.1f, scene_radius * 4.0f,
              projection);

    glm_mat4_mul(projection, view, shadow_map->light_space_matrix);
}

void shadow_map_get_light_space_matrix(const ShadowMap *shadow_map, mat4 out_matrix) {
    ASSERT(shadow_map != nullptr);
    if (shadow_map == nullptr) {
        return;
    }
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out_matrix[col][row] = shadow_map->light_space_matrix[col][row];
        }
    }
}

void shadow_map_begin(ShadowMap *shadow_map) {
    ASSERT(shadow_map != nullptr);
    if (shadow_map == nullptr) {
        return;
    }
    framebuffer_bind(shadow_map->framebuffer);
    /* GL_DEPTH_TEST must be enabled for the depth buffer to be written
     * at all, not merely to be tested against - a classic OpenGL trap,
     * since depth test defaults to disabled and this pass never draws
     * color, so there is nothing else that would surface the bug. */
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(shadow_map->gl_program);
    glUniformMatrix4fv(shadow_map->light_space_matrix_location, 1, GL_FALSE,
                        (const float *)shadow_map->light_space_matrix);
}

void shadow_map_draw(const ShadowMap *shadow_map, mat4 model_matrix, uint32_t vertex_array,
                      uint32_t vertex_count, uint32_t index_count) {
    ASSERT(shadow_map != nullptr);
    if (shadow_map == nullptr) {
        return;
    }
    glUniformMatrix4fv(shadow_map->model_location, 1, GL_FALSE, (const float *)model_matrix);
    glBindVertexArray(vertex_array);
    if (index_count > 0) {
        glDrawElements(GL_TRIANGLES, (GLsizei)index_count, GL_UNSIGNED_INT, nullptr);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertex_count);
    }
}

uint32_t shadow_map_get_depth_texture(const ShadowMap *shadow_map) {
    ASSERT(shadow_map != nullptr);
    return shadow_map != nullptr ? framebuffer_get_depth_texture(shadow_map->framebuffer) : 0;
}

uint32_t shadow_map_get_resolution(const ShadowMap *shadow_map) {
    ASSERT(shadow_map != nullptr);
    return shadow_map != nullptr ? framebuffer_get_width(shadow_map->framebuffer) : 0;
}
