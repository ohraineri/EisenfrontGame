#ifndef EISENFRONT_BUFFERS_H
#define EISENFRONT_BUFFERS_H

#include "core.h"
#include "graphics_context.h"

typedef enum BufferType {
    BUFFER_TYPE_VERTEX = 0,
    BUFFER_TYPE_INDEX,
    BUFFER_TYPE_UNIFORM,
    BUFFER_TYPE_STORAGE,
} BufferType;

typedef enum BufferUsage {
    BUFFER_USAGE_STATIC = 0, /* uploaded once (or rarely), drawn many times */
    BUFFER_USAGE_DYNAMIC,    /* updated occasionally */
    BUFFER_USAGE_STREAM,     /* updated close to every frame */
} BufferUsage;

typedef struct BufferDesc {
    BufferType  type;
    BufferUsage usage;             /* ignored when persistent_mapped is true */
    const void *initial_data;      /* nullptr allocates uninitialized storage */
    size_t      size_bytes;
    bool        persistent_mapped; /* see the file header comment */
} BufferDesc;

typedef struct GpuBuffer GpuBuffer;

ENGINE_API Result gpu_buffer_create(const BufferDesc *desc, GpuBuffer **out_buffer);
ENGINE_API void   gpu_buffer_destroy(GpuBuffer *buffer);

ENGINE_API uint32_t gpu_buffer_get_gl_handle(const GpuBuffer *buffer);
ENGINE_API size_t   gpu_buffer_get_size(const GpuBuffer *buffer);

/* Only valid for buffers created with persistent_mapped == false;
 * persistently mapped buffers are immutable storage and are written
 * through the pointer from gpu_buffer_get_mapped_pointer() instead. */
ENGINE_API Result gpu_buffer_update(GpuBuffer *buffer, size_t offset, const void *data,
                                     size_t size_bytes);

/* Only valid for buffers created with persistent_mapped == true;
 * returns nullptr otherwise. See the file header comment on
 * synchronization before writing through this pointer every frame. */
ENGINE_API void *gpu_buffer_get_mapped_pointer(const GpuBuffer *buffer);

/* Binds a UNIFORM or STORAGE buffer to an indexed binding point
 * (glBindBufferBase / glBindBufferRange); invalid for VERTEX/INDEX
 * buffers, which bind through a VertexArray instead. */
ENGINE_API Result gpu_buffer_bind_indexed(GpuBuffer *buffer, uint32_t binding_index);
ENGINE_API Result gpu_buffer_bind_indexed_range(GpuBuffer *buffer, uint32_t binding_index,
                                                 size_t offset, size_t size_bytes);

typedef enum VertexAttribType {
    VERTEX_ATTRIB_TYPE_FLOAT = 0,
    VERTEX_ATTRIB_TYPE_INT,
    VERTEX_ATTRIB_TYPE_UNSIGNED_BYTE,
} VertexAttribType;

typedef struct VertexAttribute {
    uint32_t         location;
    uint32_t         component_count; /* 1-4 */
    VertexAttribType type;
    bool             normalized; /* ignored for VERTEX_ATTRIB_TYPE_INT (glVertexAttribIPointer) */
    uint32_t         stride_bytes;
    size_t           offset_bytes;
    uint32_t         instance_divisor; /* 0 = per-vertex; N>0 = advances once per N instances */
} VertexAttribute;

/* One vertex buffer, interleaved, describes every attribute; use
 * separate GpuBuffers plus separate VertexArrays if you need
 * non-interleaved (structure-of-arrays) vertex data. */
typedef struct VertexArrayDesc {
    GpuBuffer              *vertex_buffer;
    GpuBuffer               *index_buffer; /* nullptr for non-indexed draws */
    const VertexAttribute  *attributes;
    uint32_t                 attribute_count;
} VertexArrayDesc;

typedef struct VertexArray VertexArray;

ENGINE_API Result   vertex_array_create(const VertexArrayDesc *desc, VertexArray **out_array);
ENGINE_API void     vertex_array_destroy(VertexArray *array);
ENGINE_API uint32_t vertex_array_get_gl_handle(const VertexArray *array);

#endif /* EISENFRONT_BUFFERS_H */
