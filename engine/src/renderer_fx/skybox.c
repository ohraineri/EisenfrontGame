#include "eisenfront/skybox.h"

#include "renderer_fx_internal.h"

#include <stdlib.h>

static const char *const k_skybox_vertex_source =
    "#version 460 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "out vec3 vDirection;\n"
    "uniform mat4 uViewNoTranslation;\n"
    "uniform mat4 uProjection;\n"
    "void main() {\n"
    "    vDirection = aPos;\n"
    "    vec4 pos = uProjection * uViewNoTranslation * vec4(aPos, 1.0);\n"
    "    gl_Position = pos.xyww;\n"
    "}\n";

static const char *const k_skybox_fragment_source =
    "#version 460 core\n"
    "in vec3 vDirection;\n"
    "out vec4 FragColor;\n"
    "uniform samplerCube uSkybox;\n"
    "void main() { FragColor = texture(uSkybox, vDirection); }\n";

/* Unit cube, 36 non-indexed vertices, position-only. */
static const float k_cube_vertices[108] = {
    -1, 1, -1, -1, -1, -1, 1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1,
    -1, -1, 1, -1, -1, -1, -1, 1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1,
    1, -1, -1, 1, -1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, 1, -1, -1,
    -1, -1, 1, -1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, 1, -1, -1, 1,
    -1, 1, -1, 1, 1, -1, 1, 1, 1, 1, 1, 1, -1, 1, 1, -1, 1, -1,
    -1, -1, -1, -1, -1, 1, 1, -1, -1, 1, -1, -1, -1, -1, 1, 1, -1, 1,
};

struct Skybox {
    Texture     *cubemap;
    uint32_t     gl_program;
    GpuBuffer   *vertex_buffer;
    VertexArray *vertex_array;
};

Result skybox_create(Texture *cubemap, Skybox **out_skybox) {
    if (cubemap == nullptr || out_skybox == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const GLuint program = renderer_fx_link_program(k_skybox_vertex_source, k_skybox_fragment_source);
    if (program == 0) {
        return RESULT_ERROR_PLATFORM;
    }

    const BufferDesc buffer_desc = {
        .type = BUFFER_TYPE_VERTEX,
        .usage = BUFFER_USAGE_STATIC,
        .initial_data = k_cube_vertices,
        .size_bytes = sizeof(k_cube_vertices),
    };
    GpuBuffer *vertex_buffer = nullptr;
    Result     result = gpu_buffer_create(&buffer_desc, &vertex_buffer);
    if (result != RESULT_OK) {
        glDeleteProgram(program);
        return result;
    }

    const VertexAttribute attribute = {
        .location = 0,
        .component_count = 3,
        .type = VERTEX_ATTRIB_TYPE_FLOAT,
        .stride_bytes = sizeof(float) * 3,
        .offset_bytes = 0,
    };
    const VertexArrayDesc array_desc = {
        .vertex_buffer = vertex_buffer, .attributes = &attribute, .attribute_count = 1};
    VertexArray *vertex_array = nullptr;
    result = vertex_array_create(&array_desc, &vertex_array);
    if (result != RESULT_OK) {
        gpu_buffer_destroy(vertex_buffer);
        glDeleteProgram(program);
        return result;
    }

    Skybox *skybox = malloc(sizeof(*skybox));
    if (skybox == nullptr) {
        vertex_array_destroy(vertex_array);
        gpu_buffer_destroy(vertex_buffer);
        glDeleteProgram(program);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    texture_retain(cubemap);
    skybox->cubemap = cubemap;
    skybox->gl_program = program;
    skybox->vertex_buffer = vertex_buffer;
    skybox->vertex_array = vertex_array;

    *out_skybox = skybox;
    return RESULT_OK;
}

void skybox_destroy(Skybox *skybox) {
    if (skybox == nullptr) {
        return;
    }
    texture_release(skybox->cubemap);
    vertex_array_destroy(skybox->vertex_array);
    gpu_buffer_destroy(skybox->vertex_buffer);
    glDeleteProgram(skybox->gl_program);
    free(skybox);
}

void skybox_render(const Skybox *skybox, mat4 view, mat4 projection) {
    ASSERT(skybox != nullptr);
    if (skybox == nullptr) {
        return;
    }

    /* Strip translation: copying only the upper-left 3x3 (rotation)
     * block and leaving the identity's translation/perspective row
     * means the skybox rotates with the camera but never translates
     * with it - see the file header comment. */
    mat4 view_no_translation;
    glm_mat4_copy(view, view_no_translation);
    view_no_translation[3][0] = 0.0f;
    view_no_translation[3][1] = 0.0f;
    view_no_translation[3][2] = 0.0f;

    glDepthFunc(GL_LEQUAL);
    glUseProgram(skybox->gl_program);

    const GLint view_location = glGetUniformLocation(skybox->gl_program, "uViewNoTranslation");
    const GLint projection_location = glGetUniformLocation(skybox->gl_program, "uProjection");
    glUniformMatrix4fv(view_location, 1, GL_FALSE, (const float *)view_no_translation);
    glUniformMatrix4fv(projection_location, 1, GL_FALSE, (const float *)projection);

    texture_bind(skybox->cubemap, 0);
    glUniform1i(glGetUniformLocation(skybox->gl_program, "uSkybox"), 0);

    glBindVertexArray(vertex_array_get_gl_handle(skybox->vertex_array));
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);

    glDepthFunc(GL_LESS);
}
