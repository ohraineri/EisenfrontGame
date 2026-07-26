#ifndef EISENFRONT_SHADER_H
#define EISENFRONT_SHADER_H

#include "core.h"
#include "graphics_context.h"
#include "platform.h"

ENGINE_API Result shader_system_init(void);
ENGINE_API void   shader_system_shutdown(void);

typedef struct ShaderProgramDesc {
    const char *vertex_path;
    const char *fragment_path;
    const char *geometry_path; /* nullptr or "" disables the geometry stage */
} ShaderProgramDesc;

typedef struct ShaderProgram ShaderProgram;

ENGINE_API Result shader_program_load(const ShaderProgramDesc *desc, ShaderProgram **out_program);
ENGINE_API void   shader_program_retain(ShaderProgram *program);
ENGINE_API void   shader_program_release(ShaderProgram *program);

ENGINE_API Result shader_program_reload(ShaderProgram *program);

/* See the file header comment on why this must not be cached across
 * frames if hot reload is in use. */
ENGINE_API uint32_t shader_program_get_gl_handle(const ShaderProgram *program);

ENGINE_API int32_t shader_program_get_uniform_location(const ShaderProgram *program,
                                                         const char *name);

ENGINE_API void shader_set_uniform_1i(ShaderProgram *program, const char *name, int32_t value);
ENGINE_API void shader_set_uniform_1f(ShaderProgram *program, const char *name, float value);
ENGINE_API void shader_set_uniform_2f(ShaderProgram *program, const char *name, float x, float y);
ENGINE_API void shader_set_uniform_3f(ShaderProgram *program, const char *name, float x, float y,
                                       float z);
ENGINE_API void shader_set_uniform_4f(ShaderProgram *program, const char *name, float x, float y,
                                       float z, float w);
/* matrix is 16 floats, column-major (GLSL's native layout). */
ENGINE_API void shader_set_uniform_mat4(ShaderProgram *program, const char *name,
                                         const float *matrix);

typedef struct ShaderUniformInfo {
    char   name[64];
    GLenum type;
    GLint  location;
    GLint  array_size;
} ShaderUniformInfo;

typedef struct ShaderAttributeInfo {
    char   name[64];
    GLenum type;
    GLint  location;
} ShaderAttributeInfo;

ENGINE_API uint32_t shader_program_uniform_count(const ShaderProgram *program);
ENGINE_API bool     shader_program_get_uniform_info(const ShaderProgram *program, uint32_t index,
                                                      ShaderUniformInfo *out_info);

ENGINE_API uint32_t shader_program_attribute_count(const ShaderProgram *program);
ENGINE_API bool     shader_program_get_attribute_info(const ShaderProgram *program, uint32_t index,
                                                        ShaderAttributeInfo *out_info);

#endif /* EISENFRONT_SHADER_H */
