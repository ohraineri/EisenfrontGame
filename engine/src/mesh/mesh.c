#include "eisenfront/mesh.h"

#include <stddef.h>
#include <stdlib.h>

struct Mesh {
    GpuBuffer    *vertex_buffer;
    GpuBuffer    *index_buffer; /* nullptr if non-indexed */
    VertexArray  *vertex_array;
    uint32_t      vertex_count;
    uint32_t      index_count;
    bool          dynamic;
};

Result mesh_create(const MeshDesc *desc, Mesh **out_mesh) {
    if (desc == nullptr || out_mesh == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (desc->vertices == nullptr || desc->vertex_count == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (desc->index_count > 0 && desc->indices == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const BufferUsage usage = desc->dynamic ? BUFFER_USAGE_DYNAMIC : BUFFER_USAGE_STATIC;

    const BufferDesc vertex_buffer_desc = {
        .type = BUFFER_TYPE_VERTEX,
        .usage = usage,
        .initial_data = desc->vertices,
        .size_bytes = sizeof(Vertex) * desc->vertex_count,
    };
    GpuBuffer *vertex_buffer = nullptr;
    Result     result = gpu_buffer_create(&vertex_buffer_desc, &vertex_buffer);
    if (result != RESULT_OK) {
        return result;
    }

    GpuBuffer *index_buffer = nullptr;
    if (desc->index_count > 0) {
        const BufferDesc index_buffer_desc = {
            .type = BUFFER_TYPE_INDEX,
            .usage = usage,
            .initial_data = desc->indices,
            .size_bytes = sizeof(uint32_t) * desc->index_count,
        };
        result = gpu_buffer_create(&index_buffer_desc, &index_buffer);
        if (result != RESULT_OK) {
            gpu_buffer_destroy(vertex_buffer);
            return result;
        }
    }

    const VertexAttribute attributes[6] = {
        {.location = 0,
         .component_count = 3,
         .type = VERTEX_ATTRIB_TYPE_FLOAT,
         .stride_bytes = sizeof(Vertex),
         .offset_bytes = offsetof(Vertex, position)},
        {.location = 1,
         .component_count = 3,
         .type = VERTEX_ATTRIB_TYPE_FLOAT,
         .stride_bytes = sizeof(Vertex),
         .offset_bytes = offsetof(Vertex, normal)},
        {.location = 2,
         .component_count = 3,
         .type = VERTEX_ATTRIB_TYPE_FLOAT,
         .stride_bytes = sizeof(Vertex),
         .offset_bytes = offsetof(Vertex, tangent)},
        {.location = 3,
         .component_count = 2,
         .type = VERTEX_ATTRIB_TYPE_FLOAT,
         .stride_bytes = sizeof(Vertex),
         .offset_bytes = offsetof(Vertex, uv)},
        {.location = 4,
         .component_count = 4,
         .type = VERTEX_ATTRIB_TYPE_INT,
         .stride_bytes = sizeof(Vertex),
         .offset_bytes = offsetof(Vertex, bone_indices)},
        {.location = 5,
         .component_count = 4,
         .type = VERTEX_ATTRIB_TYPE_FLOAT,
         .stride_bytes = sizeof(Vertex),
         .offset_bytes = offsetof(Vertex, bone_weights)},
    };

    const VertexArrayDesc array_desc = {
        .vertex_buffer = vertex_buffer,
        .index_buffer = index_buffer,
        .attributes = attributes,
        .attribute_count = 6,
    };
    VertexArray *vertex_array = nullptr;
    result = vertex_array_create(&array_desc, &vertex_array);
    if (result != RESULT_OK) {
        gpu_buffer_destroy(vertex_buffer);
        gpu_buffer_destroy(index_buffer);
        return result;
    }

    Mesh *mesh = malloc(sizeof(*mesh));
    if (mesh == nullptr) {
        vertex_array_destroy(vertex_array);
        gpu_buffer_destroy(vertex_buffer);
        gpu_buffer_destroy(index_buffer);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    mesh->vertex_buffer = vertex_buffer;
    mesh->index_buffer = index_buffer;
    mesh->vertex_array = vertex_array;
    mesh->vertex_count = desc->vertex_count;
    mesh->index_count = desc->index_count;
    mesh->dynamic = desc->dynamic;

    *out_mesh = mesh;
    return RESULT_OK;
}

void mesh_destroy(Mesh *mesh) {
    if (mesh == nullptr) {
        return;
    }
    vertex_array_destroy(mesh->vertex_array);
    gpu_buffer_destroy(mesh->vertex_buffer);
    gpu_buffer_destroy(mesh->index_buffer);
    free(mesh);
}

Result mesh_update_vertices(Mesh *mesh, uint32_t offset, const Vertex *vertices, uint32_t count) {
    if (mesh == nullptr || vertices == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!mesh->dynamic) {
        return RESULT_ERROR_INVALID_STATE;
    }
    if (offset + count > mesh->vertex_count) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    return gpu_buffer_update(mesh->vertex_buffer, (size_t)offset * sizeof(Vertex), vertices,
                              (size_t)count * sizeof(Vertex));
}

Result mesh_update_indices(Mesh *mesh, uint32_t offset, const uint32_t *indices, uint32_t count) {
    if (mesh == nullptr || indices == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!mesh->dynamic) {
        return RESULT_ERROR_INVALID_STATE;
    }
    if (mesh->index_buffer == nullptr) {
        return RESULT_ERROR_INVALID_STATE;
    }
    if (offset + count > mesh->index_count) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    return gpu_buffer_update(mesh->index_buffer, (size_t)offset * sizeof(uint32_t), indices,
                              (size_t)count * sizeof(uint32_t));
}

static GLenum gl_type_for_mesh_attrib(VertexAttribType type) {
    switch (type) {
        case VERTEX_ATTRIB_TYPE_INT:
            return GL_INT;
        case VERTEX_ATTRIB_TYPE_UNSIGNED_BYTE:
            return GL_UNSIGNED_BYTE;
        case VERTEX_ATTRIB_TYPE_FLOAT:
        default:
            return GL_FLOAT;
    }
}

Result mesh_set_instance_buffer(Mesh *mesh, GpuBuffer *instance_buffer,
                                 const VertexAttribute *attributes, uint32_t attribute_count) {
    if (mesh == nullptr || instance_buffer == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (attribute_count > 0 && attributes == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    glBindVertexArray(mesh_get_vertex_array_handle(mesh));
    glBindBuffer(GL_ARRAY_BUFFER, gpu_buffer_get_gl_handle(instance_buffer));

    for (uint32_t i = 0; i < attribute_count; ++i) {
        const VertexAttribute *attribute = &attributes[i];
        glEnableVertexAttribArray(attribute->location);

        const GLenum gl_type = gl_type_for_mesh_attrib(attribute->type);
        const void  *offset_ptr = (const void *)attribute->offset_bytes;
        if (attribute->type == VERTEX_ATTRIB_TYPE_INT) {
            glVertexAttribIPointer(attribute->location, (GLint)attribute->component_count,
                                    gl_type, (GLsizei)attribute->stride_bytes, offset_ptr);
        } else {
            glVertexAttribPointer(attribute->location, (GLint)attribute->component_count, gl_type,
                                   attribute->normalized ? GL_TRUE : GL_FALSE,
                                   (GLsizei)attribute->stride_bytes, offset_ptr);
        }
        /* Per-instance attributes must advance once per instance, not
         * once per vertex; default to 1 if the caller left it at 0. */
        glVertexAttribDivisor(attribute->location,
                               attribute->instance_divisor > 0 ? attribute->instance_divisor : 1);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return RESULT_OK;
}

uint32_t mesh_get_vertex_array_handle(const Mesh *mesh) {
    ASSERT(mesh != nullptr);
    return mesh != nullptr ? vertex_array_get_gl_handle(mesh->vertex_array) : 0;
}

uint32_t mesh_get_vertex_count(const Mesh *mesh) {
    ASSERT(mesh != nullptr);
    return mesh != nullptr ? mesh->vertex_count : 0;
}

uint32_t mesh_get_index_count(const Mesh *mesh) {
    ASSERT(mesh != nullptr);
    return mesh != nullptr ? mesh->index_count : 0;
}

bool mesh_is_indexed(const Mesh *mesh) {
    ASSERT(mesh != nullptr);
    return mesh != nullptr && mesh->index_buffer != nullptr;
}
