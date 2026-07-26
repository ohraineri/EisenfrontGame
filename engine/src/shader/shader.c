#include "eisenfront/shader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHADER_LOG_CATEGORY "shader"
#define SHADER_PATH_CAPACITY 256u
#define SHADER_CACHE_CAPACITY 256u
#define SHADER_INFO_LOG_CAPACITY 2048u

struct ShaderProgram {
    uint32_t gl_program;
    char     vertex_path[SHADER_PATH_CAPACITY];
    char     fragment_path[SHADER_PATH_CAPACITY];
    char     geometry_path[SHADER_PATH_CAPACITY]; /* empty string when unused */
    uint32_t ref_count;

    ShaderUniformInfo   *uniforms;
    uint32_t              uniform_count;
    ShaderAttributeInfo  *attributes;
    uint32_t              attribute_count;
};

typedef struct ShaderCacheEntry {
    char            key[SHADER_PATH_CAPACITY * 3];
    ShaderProgram  *program;
} ShaderCacheEntry;

static bool             g_initialized = false;
static ShaderCacheEntry g_cache[SHADER_CACHE_CAPACITY];
static uint32_t         g_cache_count = 0;

Result shader_system_init(void) {
    if (g_initialized) {
        return RESULT_ERROR_ALREADY_INITIALIZED;
    }
    memset(g_cache, 0, sizeof(g_cache));
    g_cache_count = 0;
    g_initialized = true;
    return RESULT_OK;
}

void shader_system_shutdown(void) {
    if (!g_initialized) {
        return;
    }
    if (g_cache_count > 0) {
        LOG_WARN(SHADER_LOG_CATEGORY,
                 "shader_system_shutdown with %u program(s) still cached; releasing them",
                 g_cache_count);
    }
    for (uint32_t i = 0; i < g_cache_count; ++i) {
        ShaderProgram *program = g_cache[i].program;
        glDeleteProgram(program->gl_program);
        free(program->uniforms);
        free(program->attributes);
        free(program);
    }
    g_cache_count = 0;
    g_initialized = false;
}

static void build_cache_key(const ShaderProgramDesc *desc, char *out_key, size_t capacity) {
    const char *geometry = (desc->geometry_path != nullptr) ? desc->geometry_path : "";
    snprintf(out_key, capacity, "%s|%s|%s", desc->vertex_path, desc->fragment_path, geometry);
}

static Result compile_stage(GLenum gl_stage, const char *path, GLuint *out_shader) {
    void  *source_data = nullptr;
    size_t source_size = 0;
    Result result = file_read_all(path, &source_data, &source_size);
    if (result != RESULT_OK) {
        LOG_ERROR(SHADER_LOG_CATEGORY, "Failed to read shader source '%s': %s", path,
                  result_to_string(result));
        return result;
    }

    const GLuint  shader = glCreateShader(gl_stage);
    const GLchar *source_ptr = (const GLchar *)source_data;
    const GLint   source_length = (GLint)source_size;
    glShaderSource(shader, 1, &source_ptr, &source_length);
    glCompileShader(shader);
    file_free_buffer(source_data);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        char log[SHADER_INFO_LOG_CAPACITY];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LOG_ERROR(SHADER_LOG_CATEGORY, "Failed to compile '%s':\n%s", path, log);
        glDeleteShader(shader);
        return RESULT_ERROR_PLATFORM;
    }

    *out_shader = shader;
    return RESULT_OK;
}

static Result link_program(const ShaderProgramDesc *desc, GLuint *out_program) {
    const bool has_geometry = desc->geometry_path != nullptr && desc->geometry_path[0] != '\0';

    GLuint vertex_shader = 0;
    Result result = compile_stage(GL_VERTEX_SHADER, desc->vertex_path, &vertex_shader);
    if (result != RESULT_OK) {
        return result;
    }

    GLuint fragment_shader = 0;
    result = compile_stage(GL_FRAGMENT_SHADER, desc->fragment_path, &fragment_shader);
    if (result != RESULT_OK) {
        glDeleteShader(vertex_shader);
        return result;
    }

    GLuint geometry_shader = 0;
    if (has_geometry) {
        result = compile_stage(GL_GEOMETRY_SHADER, desc->geometry_path, &geometry_shader);
        if (result != RESULT_OK) {
            glDeleteShader(vertex_shader);
            glDeleteShader(fragment_shader);
            return result;
        }
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    if (has_geometry) {
        glAttachShader(program, geometry_shader);
    }
    glLinkProgram(program);

    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);

    /* Safe to delete unconditionally here regardless of link outcome -
     * see the file header comment on stage object lifetime. */
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    if (has_geometry) {
        glDeleteShader(geometry_shader);
    }

    if (status != GL_TRUE) {
        char log[SHADER_INFO_LOG_CAPACITY];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        LOG_ERROR(SHADER_LOG_CATEGORY, "Failed to link program (%s, %s%s%s):\n%s",
                  desc->vertex_path, desc->fragment_path, has_geometry ? ", " : "",
                  has_geometry ? desc->geometry_path : "", log);
        glDeleteProgram(program);
        return RESULT_ERROR_PLATFORM;
    }

    *out_program = program;
    return RESULT_OK;
}

static void free_reflection(ShaderProgram *program) {
    free(program->uniforms);
    free(program->attributes);
    program->uniforms = nullptr;
    program->attributes = nullptr;
    program->uniform_count = 0;
    program->attribute_count = 0;
}

static Result build_reflection(ShaderProgram *program) {
    GLint uniform_count = 0;
    glGetProgramiv(program->gl_program, GL_ACTIVE_UNIFORMS, &uniform_count);
    GLint attribute_count = 0;
    glGetProgramiv(program->gl_program, GL_ACTIVE_ATTRIBUTES, &attribute_count);

    ShaderUniformInfo *uniforms = nullptr;
    if (uniform_count > 0) {
        uniforms = malloc(sizeof(ShaderUniformInfo) * (size_t)uniform_count);
        if (uniforms == nullptr) {
            return RESULT_ERROR_OUT_OF_MEMORY;
        }
    }
    ShaderAttributeInfo *attributes = nullptr;
    if (attribute_count > 0) {
        attributes = malloc(sizeof(ShaderAttributeInfo) * (size_t)attribute_count);
        if (attributes == nullptr) {
            free(uniforms);
            return RESULT_ERROR_OUT_OF_MEMORY;
        }
    }

    for (GLint i = 0; i < uniform_count; ++i) {
        ShaderUniformInfo *info = &uniforms[i];
        GLsizei             written = 0;
        glGetActiveUniform(program->gl_program, (GLuint)i, sizeof(info->name), &written,
                            &info->array_size, &info->type, info->name);
        info->location = glGetUniformLocation(program->gl_program, info->name);
    }
    for (GLint i = 0; i < attribute_count; ++i) {
        ShaderAttributeInfo *info = &attributes[i];
        GLsizei               written = 0;
        GLint                 size = 0;
        glGetActiveAttrib(program->gl_program, (GLuint)i, sizeof(info->name), &written, &size,
                           &info->type, info->name);
        info->location = glGetAttribLocation(program->gl_program, info->name);
    }

    program->uniforms = uniforms;
    program->uniform_count = (uint32_t)uniform_count;
    program->attributes = attributes;
    program->attribute_count = (uint32_t)attribute_count;
    return RESULT_OK;
}

Result shader_program_load(const ShaderProgramDesc *desc, ShaderProgram **out_program) {
    if (desc == nullptr || out_program == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (desc->vertex_path == nullptr || desc->fragment_path == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!g_initialized) {
        return RESULT_ERROR_NOT_INITIALIZED;
    }

    char key[SHADER_PATH_CAPACITY * 3];
    build_cache_key(desc, key, sizeof(key));

    for (uint32_t i = 0; i < g_cache_count; ++i) {
        if (strcmp(g_cache[i].key, key) == 0) {
            g_cache[i].program->ref_count += 1;
            *out_program = g_cache[i].program;
            return RESULT_OK;
        }
    }

    GLuint gl_program = 0;
    Result result = link_program(desc, &gl_program);
    if (result != RESULT_OK) {
        return result;
    }

    ShaderProgram *program = malloc(sizeof(*program));
    if (program == nullptr) {
        glDeleteProgram(gl_program);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    program->gl_program = gl_program;
    snprintf(program->vertex_path, sizeof(program->vertex_path), "%s", desc->vertex_path);
    snprintf(program->fragment_path, sizeof(program->fragment_path), "%s", desc->fragment_path);
    snprintf(program->geometry_path, sizeof(program->geometry_path), "%s",
              (desc->geometry_path != nullptr) ? desc->geometry_path : "");
    program->ref_count = 1;
    program->uniforms = nullptr;
    program->uniform_count = 0;
    program->attributes = nullptr;
    program->attribute_count = 0;

    result = build_reflection(program);
    if (result != RESULT_OK) {
        glDeleteProgram(gl_program);
        free(program);
        return result;
    }

    if (g_cache_count < SHADER_CACHE_CAPACITY) {
        snprintf(g_cache[g_cache_count].key, sizeof(g_cache[g_cache_count].key), "%s", key);
        g_cache[g_cache_count].program = program;
        g_cache_count += 1;
    } else {
        LOG_WARN(SHADER_LOG_CATEGORY, "Shader cache full (%u); '%s' will recompile on next load",
                 SHADER_CACHE_CAPACITY, key);
    }

    LOG_INFO(SHADER_LOG_CATEGORY, "Compiled shader program '%s'", key);
    *out_program = program;
    return RESULT_OK;
}

void shader_program_retain(ShaderProgram *program) {
    ASSERT(program != nullptr);
    if (program == nullptr) {
        return;
    }
    program->ref_count += 1;
}

void shader_program_release(ShaderProgram *program) {
    ASSERT(program != nullptr);
    if (program == nullptr) {
        return;
    }
    ASSERT_MSG(program->ref_count > 0, "shader_program_release called more times than retained");
    if (program->ref_count == 0) {
        return;
    }

    program->ref_count -= 1;
    if (program->ref_count > 0) {
        return;
    }

    for (uint32_t i = 0; i < g_cache_count; ++i) {
        if (g_cache[i].program == program) {
            g_cache[i] = g_cache[g_cache_count - 1];
            g_cache_count -= 1;
            break;
        }
    }

    glDeleteProgram(program->gl_program);
    free_reflection(program);
    free(program);
}

Result shader_program_reload(ShaderProgram *program) {
    ASSERT(program != nullptr);
    if (program == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const ShaderProgramDesc desc = {
        .vertex_path = program->vertex_path,
        .fragment_path = program->fragment_path,
        .geometry_path = program->geometry_path[0] != '\0' ? program->geometry_path : nullptr,
    };

    GLuint new_gl_program = 0;
    Result result = link_program(&desc, &new_gl_program);
    if (result != RESULT_OK) {
        LOG_WARN(SHADER_LOG_CATEGORY,
                 "Hot reload failed for '%s'; keeping the last working program",
                 program->vertex_path);
        return result;
    }

    const GLuint old_gl_program = program->gl_program;
    program->gl_program = new_gl_program;
    free_reflection(program);

    result = build_reflection(program);
    if (result != RESULT_OK) {
        /* Reflection allocation failed after a successful relink: keep
         * the new program (it is valid GL state) but report the error
         * so the caller knows uniform/attribute queries are unavailable
         * until the next successful reload. */
        glDeleteProgram(old_gl_program);
        return result;
    }

    glDeleteProgram(old_gl_program);
    LOG_INFO(SHADER_LOG_CATEGORY, "Reloaded shader program '%s'", program->vertex_path);
    return RESULT_OK;
}

uint32_t shader_program_get_gl_handle(const ShaderProgram *program) {
    ASSERT(program != nullptr);
    return program != nullptr ? program->gl_program : 0;
}

int32_t shader_program_get_uniform_location(const ShaderProgram *program, const char *name) {
    ASSERT(program != nullptr);
    ASSERT(name != nullptr);
    if (program == nullptr || name == nullptr) {
        return -1;
    }
    for (uint32_t i = 0; i < program->uniform_count; ++i) {
        if (strcmp(program->uniforms[i].name, name) == 0) {
            return program->uniforms[i].location;
        }
    }
    return -1;
}

void shader_set_uniform_1i(ShaderProgram *program, const char *name, int32_t value) {
    const int32_t location = shader_program_get_uniform_location(program, name);
    if (location >= 0) {
        glProgramUniform1i(program->gl_program, location, value);
    }
}

void shader_set_uniform_1f(ShaderProgram *program, const char *name, float value) {
    const int32_t location = shader_program_get_uniform_location(program, name);
    if (location >= 0) {
        glProgramUniform1f(program->gl_program, location, value);
    }
}

void shader_set_uniform_2f(ShaderProgram *program, const char *name, float x, float y) {
    const int32_t location = shader_program_get_uniform_location(program, name);
    if (location >= 0) {
        glProgramUniform2f(program->gl_program, location, x, y);
    }
}

void shader_set_uniform_3f(ShaderProgram *program, const char *name, float x, float y, float z) {
    const int32_t location = shader_program_get_uniform_location(program, name);
    if (location >= 0) {
        glProgramUniform3f(program->gl_program, location, x, y, z);
    }
}

void shader_set_uniform_4f(ShaderProgram *program, const char *name, float x, float y, float z,
                            float w) {
    const int32_t location = shader_program_get_uniform_location(program, name);
    if (location >= 0) {
        glProgramUniform4f(program->gl_program, location, x, y, z, w);
    }
}

void shader_set_uniform_mat4(ShaderProgram *program, const char *name, const float *matrix) {
    ASSERT(matrix != nullptr);
    const int32_t location = shader_program_get_uniform_location(program, name);
    if (location >= 0 && matrix != nullptr) {
        glProgramUniformMatrix4fv(program->gl_program, location, 1, GL_FALSE, matrix);
    }
}

uint32_t shader_program_uniform_count(const ShaderProgram *program) {
    ASSERT(program != nullptr);
    return program != nullptr ? program->uniform_count : 0;
}

bool shader_program_get_uniform_info(const ShaderProgram *program, uint32_t index,
                                      ShaderUniformInfo *out_info) {
    ASSERT(program != nullptr);
    ASSERT(out_info != nullptr);
    if (program == nullptr || out_info == nullptr || index >= program->uniform_count) {
        return false;
    }
    *out_info = program->uniforms[index];
    return true;
}

uint32_t shader_program_attribute_count(const ShaderProgram *program) {
    ASSERT(program != nullptr);
    return program != nullptr ? program->attribute_count : 0;
}

bool shader_program_get_attribute_info(const ShaderProgram *program, uint32_t index,
                                        ShaderAttributeInfo *out_info) {
    ASSERT(program != nullptr);
    ASSERT(out_info != nullptr);
    if (program == nullptr || out_info == nullptr || index >= program->attribute_count) {
        return false;
    }
    *out_info = program->attributes[index];
    return true;
}
