/*
 * Private helper shared by skybox.c, shadow.c and postprocess.c: each
 * owns a small, fixed GLSL shader compiled from an embedded string
 * (there is no source file, so Shader module's file-based loader does
 * not apply here - this is a deliberately minimal, self-contained
 * compile+link, not a replacement for it).
 */
#ifndef EISENFRONT_RENDERER_FX_INTERNAL_H
#define EISENFRONT_RENDERER_FX_INTERNAL_H

#include "eisenfront/core.h"
#include "eisenfront/graphics_context.h"

static inline GLuint renderer_fx_compile_stage(GLenum stage, const char *source) {
    const GLuint shader = glCreateShader(stage);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LOG_ERROR("renderer_fx", "Embedded shader failed to compile:\n%s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static inline GLuint renderer_fx_link_program(const char *vertex_source,
                                               const char *fragment_source) {
    const GLuint vertex_shader = renderer_fx_compile_stage(GL_VERTEX_SHADER, vertex_source);
    if (vertex_shader == 0) {
        return 0;
    }
    const GLuint fragment_shader = renderer_fx_compile_stage(GL_FRAGMENT_SHADER, fragment_source);
    if (fragment_shader == 0) {
        glDeleteShader(vertex_shader);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    if (status != GL_TRUE) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        LOG_ERROR("renderer_fx", "Embedded shader program failed to link:\n%s", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

#endif /* EISENFRONT_RENDERER_FX_INTERNAL_H */
