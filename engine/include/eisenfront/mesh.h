/*
 * Mesh - a reusable geometry abstraction on top of Buffers: one
 * interleaved Vertex format covering position, normal, tangent, UV and
 * skinning data, static or dynamic storage, and optional per-instance
 * attributes for instanced drawing.
 *
 * Vertex layout: rather than a fully generic configurable vertex
 * format, every Mesh uses the same interleaved Vertex struct (position,
 * normal, tangent, uv, bone_indices, bone_weights) bound to attribute
 * locations 0-5. A static (non-skinned) mesh simply leaves
 * bone_indices/bone_weights at zero; a shader that never reads those
 * attributes ignores them for free. This trades format flexibility for
 * simplicity: one mesh code path serves both static and skeletal
 * meshes, and one shader vertex layout serves every Mesh, which is
 * enough for a single engine's worth of meshes without a much larger
 * generic-vertex-format system.
 *
 * Static vs dynamic: MeshDesc.dynamic selects the underlying GpuBuffers'
 * usage hint (BUFFER_USAGE_STATIC vs BUFFER_USAGE_DYNAMIC, see
 * buffers.h) and gates mesh_update_vertices()/mesh_update_indices() -
 * both return RESULT_ERROR_INVALID_STATE on a mesh created static. This
 * covers meshes updated occasionally (a deforming mesh, a dynamic UI
 * quad); for maximum-throughput every-frame streaming, build a
 * persistent-mapped GpuBuffer directly through buffers.h instead.
 *
 * Instancing: mesh_set_instance_buffer() adds extra per-instance vertex
 * attributes (typically a 4x4 transform, itself 4 consecutive vec4
 * attribute locations) to the mesh's existing vertex array, each with
 * an instance divisor so it advances once per instance rather than once
 * per vertex. The instance count itself is not stored on the Mesh - it
 * is a Renderer::RenderCommand field, since the same Mesh can be drawn
 * with a different instance count from frame to frame.
 *
 * Dependencies: Core, Graphics Context, Buffers.
 */
#ifndef EISENFRONT_MESH_H
#define EISENFRONT_MESH_H

#include "buffers.h"
#include "core.h"
#include "graphics_context.h"

typedef struct Vertex {
    float    position[3];
    float    normal[3];
    float    tangent[3];
    float    uv[2];
    uint32_t bone_indices[4];
    float    bone_weights[4];
} Vertex;

typedef struct MeshDesc {
    const Vertex   *vertices;
    uint32_t         vertex_count;
    const uint32_t *indices; /* nullptr for a non-indexed mesh */
    uint32_t         index_count;
    bool             dynamic;
} MeshDesc;

typedef struct Mesh Mesh;

ENGINE_API Result mesh_create(const MeshDesc *desc, Mesh **out_mesh);
ENGINE_API void   mesh_destroy(Mesh *mesh);

/* Only valid for meshes created with MeshDesc.dynamic == true. */
ENGINE_API Result mesh_update_vertices(Mesh *mesh, uint32_t offset, const Vertex *vertices,
                                        uint32_t count);
ENGINE_API Result mesh_update_indices(Mesh *mesh, uint32_t offset, const uint32_t *indices,
                                       uint32_t count);

/* Adds per-instance vertex attributes sourced from instance_buffer to
 * this mesh's vertex array; see the file header comment on instancing.
 * instance_buffer must outlive the mesh (or until this is called again
 * with a different buffer) - the mesh does not take ownership of it. */
ENGINE_API Result mesh_set_instance_buffer(Mesh *mesh, GpuBuffer *instance_buffer,
                                            const VertexAttribute *attributes,
                                            uint32_t attribute_count);

ENGINE_API uint32_t mesh_get_vertex_array_handle(const Mesh *mesh);
ENGINE_API uint32_t mesh_get_vertex_count(const Mesh *mesh);
ENGINE_API uint32_t mesh_get_index_count(const Mesh *mesh);
ENGINE_API bool     mesh_is_indexed(const Mesh *mesh);

#endif /* EISENFRONT_MESH_H */
