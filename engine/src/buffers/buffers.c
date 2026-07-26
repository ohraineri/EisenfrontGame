#include "eisenfront/buffers.h"

#include <stdlib.h>

struct GpuBuffer {
    uint32_t   gl_buffer;
    GLenum     gl_target;
    BufferType type;
    size_t     size_bytes;
    bool       persistent_mapped;
    void      *mapped_pointer;
};

struct VertexArray {
    uint32_t gl_vao;
};

static GLenum gl_target_for_type(BufferType type) {
    switch (type) {
        case BUFFER_TYPE_VERTEX:
            return GL_ARRAY_BUFFER;
        case BUFFER_TYPE_INDEX:
            return GL_ELEMENT_ARRAY_BUFFER;
        case BUFFER_TYPE_UNIFORM:
            return GL_UNIFORM_BUFFER;
        case BUFFER_TYPE_STORAGE:
        default:
            return GL_SHADER_STORAGE_BUFFER;
    }
}

static GLenum gl_usage_for(BufferUsage usage) {
    switch (usage) {
        case BUFFER_USAGE_STATIC:
            return GL_STATIC_DRAW;
        case BUFFER_USAGE_DYNAMIC:
            return GL_DYNAMIC_DRAW;
        case BUFFER_USAGE_STREAM:
        default:
            return GL_STREAM_DRAW;
    }
}

Result gpu_buffer_create(const BufferDesc *desc, GpuBuffer **out_buffer) {
    if (desc == nullptr || out_buffer == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (desc->size_bytes == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    GpuBuffer *buffer = malloc(sizeof(*buffer));
    if (buffer == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    GLuint gl_buffer = 0;
    glGenBuffers(1, &gl_buffer);
    const GLenum target = gl_target_for_type(desc->type);
    glBindBuffer(target, gl_buffer);

    void *mapped_pointer = nullptr;
    if (desc->persistent_mapped) {
        const GLbitfield flags = GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;
        glBufferStorage(target, (GLsizeiptr)desc->size_bytes, desc->initial_data, flags);
        mapped_pointer = glMapBufferRange(target, 0, (GLsizeiptr)desc->size_bytes, flags);
        if (mapped_pointer == nullptr) {
            glBindBuffer(target, 0);
            glDeleteBuffers(1, &gl_buffer);
            free(buffer);
            return RESULT_ERROR_PLATFORM;
        }
    } else {
        glBufferData(target, (GLsizeiptr)desc->size_bytes, desc->initial_data,
                     gl_usage_for(desc->usage));
    }
    glBindBuffer(target, 0);

    buffer->gl_buffer = gl_buffer;
    buffer->gl_target = target;
    buffer->type = desc->type;
    buffer->size_bytes = desc->size_bytes;
    buffer->persistent_mapped = desc->persistent_mapped;
    buffer->mapped_pointer = mapped_pointer;

    *out_buffer = buffer;
    return RESULT_OK;
}

void gpu_buffer_destroy(GpuBuffer *buffer) {
    if (buffer == nullptr) {
        return;
    }
    if (buffer->persistent_mapped) {
        glBindBuffer(buffer->gl_target, buffer->gl_buffer);
        glUnmapBuffer(buffer->gl_target);
        glBindBuffer(buffer->gl_target, 0);
    }
    glDeleteBuffers(1, &buffer->gl_buffer);
    free(buffer);
}

uint32_t gpu_buffer_get_gl_handle(const GpuBuffer *buffer) {
    ASSERT(buffer != nullptr);
    return buffer != nullptr ? buffer->gl_buffer : 0;
}

size_t gpu_buffer_get_size(const GpuBuffer *buffer) {
    ASSERT(buffer != nullptr);
    return buffer != nullptr ? buffer->size_bytes : 0;
}

Result gpu_buffer_update(GpuBuffer *buffer, size_t offset, const void *data, size_t size_bytes) {
    if (buffer == nullptr || data == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (buffer->persistent_mapped) {
        return RESULT_ERROR_INVALID_STATE;
    }
    if (offset + size_bytes > buffer->size_bytes) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    glBindBuffer(buffer->gl_target, buffer->gl_buffer);
    glBufferSubData(buffer->gl_target, (GLintptr)offset, (GLsizeiptr)size_bytes, data);
    glBindBuffer(buffer->gl_target, 0);
    return RESULT_OK;
}

void *gpu_buffer_get_mapped_pointer(const GpuBuffer *buffer) {
    ASSERT(buffer != nullptr);
    return buffer != nullptr ? buffer->mapped_pointer : nullptr;
}

Result gpu_buffer_bind_indexed(GpuBuffer *buffer, uint32_t binding_index) {
    if (buffer == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (buffer->type != BUFFER_TYPE_UNIFORM && buffer->type != BUFFER_TYPE_STORAGE) {
        return RESULT_ERROR_INVALID_STATE;
    }
    glBindBufferBase(buffer->gl_target, binding_index, buffer->gl_buffer);
    return RESULT_OK;
}

Result gpu_buffer_bind_indexed_range(GpuBuffer *buffer, uint32_t binding_index, size_t offset,
                                      size_t size_bytes) {
    if (buffer == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (buffer->type != BUFFER_TYPE_UNIFORM && buffer->type != BUFFER_TYPE_STORAGE) {
        return RESULT_ERROR_INVALID_STATE;
    }
    if (offset + size_bytes > buffer->size_bytes) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    glBindBufferRange(buffer->gl_target, binding_index, buffer->gl_buffer, (GLintptr)offset,
                       (GLsizeiptr)size_bytes);
    return RESULT_OK;
}

static GLenum gl_type_for_attrib(VertexAttribType type) {
    switch (type) {
        case VERTEX_ATTRIB_TYPE_FLOAT:
            return GL_FLOAT;
        case VERTEX_ATTRIB_TYPE_INT:
            return GL_INT;
        case VERTEX_ATTRIB_TYPE_UNSIGNED_BYTE:
        default:
            return GL_UNSIGNED_BYTE;
    }
}

Result vertex_array_create(const VertexArrayDesc *desc, VertexArray **out_array) {
    if (desc == nullptr || out_array == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (desc->vertex_buffer == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (desc->attribute_count > 0 && desc->attributes == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, gpu_buffer_get_gl_handle(desc->vertex_buffer));

    for (uint32_t i = 0; i < desc->attribute_count; ++i) {
        const VertexAttribute *attribute = &desc->attributes[i];
        glEnableVertexAttribArray(attribute->location);

        const GLenum gl_type = gl_type_for_attrib(attribute->type);
        const void  *offset_ptr = (const void *)attribute->offset_bytes;
        if (attribute->type == VERTEX_ATTRIB_TYPE_INT) {
            glVertexAttribIPointer(attribute->location, (GLint)attribute->component_count,
                                    gl_type, (GLsizei)attribute->stride_bytes, offset_ptr);
        } else {
            glVertexAttribPointer(attribute->location, (GLint)attribute->component_count, gl_type,
                                   attribute->normalized ? GL_TRUE : GL_FALSE,
                                   (GLsizei)attribute->stride_bytes, offset_ptr);
        }
        if (attribute->instance_divisor > 0) {
            glVertexAttribDivisor(attribute->location, attribute->instance_divisor);
        }
    }

    if (desc->index_buffer != nullptr) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpu_buffer_get_gl_handle(desc->index_buffer));
    }

    /* Unbind the VAO first: the element array buffer binding is part of
     * VAO state, so unbinding GL_ELEMENT_ARRAY_BUFFER before the VAO
     * would clear that state back out of the VAO we just built. The
     * array buffer binding is not VAO state, so it is safe to leave
     * bound until after and clear separately. */
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    VertexArray *array = malloc(sizeof(*array));
    if (array == nullptr) {
        glDeleteVertexArrays(1, &vao);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    array->gl_vao = vao;

    *out_array = array;
    return RESULT_OK;
}

void vertex_array_destroy(VertexArray *array) {
    if (array == nullptr) {
        return;
    }
    glDeleteVertexArrays(1, &array->gl_vao);
    free(array);
}

uint32_t vertex_array_get_gl_handle(const VertexArray *array) {
    ASSERT(array != nullptr);
    return array != nullptr ? array->gl_vao : 0;
}
