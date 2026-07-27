#include "primitives.h"

static Vertex make_vertex(vec3 position, vec3 normal, vec3 tangent, float u, float v) {
    Vertex vertex = {0};
    vertex.position[0] = position[0];
    vertex.position[1] = position[1];
    vertex.position[2] = position[2];
    vertex.normal[0] = normal[0];
    vertex.normal[1] = normal[1];
    vertex.normal[2] = normal[2];
    vertex.tangent[0] = tangent[0];
    vertex.tangent[1] = tangent[1];
    vertex.tangent[2] = tangent[2];
    vertex.uv[0] = u;
    vertex.uv[1] = v;
    return vertex;
}

Result primitive_create_box(vec3 center, vec3 half_extents, float uv_scale, Mesh **out_mesh) {
    if (out_mesh == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const float cx = center[0], cy = center[1], cz = center[2];
    const float hx = half_extents[0], hy = half_extents[1], hz = half_extents[2];
    const float uw = 2.0f * hx * uv_scale;
    const float uh = 2.0f * hy * uv_scale;
    const float ud = 2.0f * hz * uv_scale;

    Vertex vertices[24];
    uint32_t v = 0;

    /* +X */
    {
        vec3 n = {1, 0, 0}, t = {0, 0, -1};
        vertices[v++] = make_vertex((vec3){cx + hx, cy - hy, cz + hz}, n, t, 0, 0);
        vertices[v++] = make_vertex((vec3){cx + hx, cy - hy, cz - hz}, n, t, ud, 0);
        vertices[v++] = make_vertex((vec3){cx + hx, cy + hy, cz - hz}, n, t, ud, uh);
        vertices[v++] = make_vertex((vec3){cx + hx, cy + hy, cz + hz}, n, t, 0, uh);
    }
    /* -X */
    {
        vec3 n = {-1, 0, 0}, t = {0, 0, 1};
        vertices[v++] = make_vertex((vec3){cx - hx, cy - hy, cz - hz}, n, t, 0, 0);
        vertices[v++] = make_vertex((vec3){cx - hx, cy - hy, cz + hz}, n, t, ud, 0);
        vertices[v++] = make_vertex((vec3){cx - hx, cy + hy, cz + hz}, n, t, ud, uh);
        vertices[v++] = make_vertex((vec3){cx - hx, cy + hy, cz - hz}, n, t, 0, uh);
    }
    /* +Y (top) */
    {
        vec3 n = {0, 1, 0}, t = {1, 0, 0};
        vertices[v++] = make_vertex((vec3){cx - hx, cy + hy, cz + hz}, n, t, 0, 0);
        vertices[v++] = make_vertex((vec3){cx + hx, cy + hy, cz + hz}, n, t, uw, 0);
        vertices[v++] = make_vertex((vec3){cx + hx, cy + hy, cz - hz}, n, t, uw, ud);
        vertices[v++] = make_vertex((vec3){cx - hx, cy + hy, cz - hz}, n, t, 0, ud);
    }
    /* -Y (bottom) */
    {
        vec3 n = {0, -1, 0}, t = {1, 0, 0};
        vertices[v++] = make_vertex((vec3){cx - hx, cy - hy, cz - hz}, n, t, 0, 0);
        vertices[v++] = make_vertex((vec3){cx + hx, cy - hy, cz - hz}, n, t, uw, 0);
        vertices[v++] = make_vertex((vec3){cx + hx, cy - hy, cz + hz}, n, t, uw, ud);
        vertices[v++] = make_vertex((vec3){cx - hx, cy - hy, cz + hz}, n, t, 0, ud);
    }
    /* +Z */
    {
        vec3 n = {0, 0, 1}, t = {1, 0, 0};
        vertices[v++] = make_vertex((vec3){cx - hx, cy - hy, cz + hz}, n, t, 0, 0);
        vertices[v++] = make_vertex((vec3){cx + hx, cy - hy, cz + hz}, n, t, uw, 0);
        vertices[v++] = make_vertex((vec3){cx + hx, cy + hy, cz + hz}, n, t, uw, uh);
        vertices[v++] = make_vertex((vec3){cx - hx, cy + hy, cz + hz}, n, t, 0, uh);
    }
    /* -Z */
    {
        vec3 n = {0, 0, -1}, t = {-1, 0, 0};
        vertices[v++] = make_vertex((vec3){cx + hx, cy - hy, cz - hz}, n, t, 0, 0);
        vertices[v++] = make_vertex((vec3){cx - hx, cy - hy, cz - hz}, n, t, uw, 0);
        vertices[v++] = make_vertex((vec3){cx - hx, cy + hy, cz - hz}, n, t, uw, uh);
        vertices[v++] = make_vertex((vec3){cx + hx, cy + hy, cz - hz}, n, t, 0, uh);
    }

    uint32_t indices[36];
    for (uint32_t face = 0; face < 6; ++face) {
        const uint32_t base_vertex = face * 4;
        const uint32_t base_index = face * 6;
        indices[base_index + 0] = base_vertex + 0;
        indices[base_index + 1] = base_vertex + 1;
        indices[base_index + 2] = base_vertex + 2;
        indices[base_index + 3] = base_vertex + 2;
        indices[base_index + 4] = base_vertex + 3;
        indices[base_index + 5] = base_vertex + 0;
    }

    const MeshDesc desc = {
        .vertices = vertices,
        .vertex_count = 24,
        .indices = indices,
        .index_count = 36,
        .dynamic = false,
    };
    return mesh_create(&desc, out_mesh);
}

Result primitive_create_ground_plane(vec3 center, float half_width, float half_depth,
                                      float uv_scale, Mesh **out_mesh) {
    if (out_mesh == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const float cx = center[0], cy = center[1], cz = center[2];
    const float uw = 2.0f * half_width * uv_scale;
    const float ud = 2.0f * half_depth * uv_scale;

    vec3 n = {0, 1, 0}, t = {1, 0, 0};
    const Vertex vertices[4] = {
        make_vertex((vec3){cx - half_width, cy, cz - half_depth}, n, t, 0, 0),
        make_vertex((vec3){cx + half_width, cy, cz - half_depth}, n, t, uw, 0),
        make_vertex((vec3){cx + half_width, cy, cz + half_depth}, n, t, uw, ud),
        make_vertex((vec3){cx - half_width, cy, cz + half_depth}, n, t, 0, ud),
    };
    const uint32_t indices[6] = {0, 1, 2, 2, 3, 0};

    const MeshDesc desc = {
        .vertices = vertices,
        .vertex_count = 4,
        .indices = indices,
        .index_count = 6,
        .dynamic = false,
    };
    return mesh_create(&desc, out_mesh);
}
