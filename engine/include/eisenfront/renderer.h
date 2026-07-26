/*
 * Renderer - frame lifecycle, a sortable render queue, and draw
 * statistics, built directly on the OpenGL objects Graphics Context
 * loaded. No lighting, PBR or shadows here - see the later renderer
 * expansion for that.
 *
 * Responsibility: gameplay never issues a gl* draw call. It (or, once
 * they exist, the Mesh/Material/Shader modules on its behalf) fills in a
 * RenderCommand referencing raw GL object names and submits it;
 * Renderer batches, sorts and executes the frame's commands. This keeps
 * Renderer usable before Shader/Buffers/Mesh exist (it only needs GL
 * object names, not those modules' types) and keeps the Renderer itself
 * with zero knowledge of gameplay, scenes or entities.
 *
 * Frame contract: call, once per frame, in this order:
 *
 *   renderer_begin_frame(renderer);         // resets the queue and this
 *                                            // frame's stats
 *   renderer_clear(renderer, ...);          // optional
 *   renderer_submit(renderer, &command);    // zero or more times
 *   renderer_end_frame(renderer);           // sorts and executes the
 *                                            // queued commands
 *   graphics_context_swap_buffers(context);  // presentation is Graphics
 *                                             // Context's job, not
 *                                             // Renderer's - a Renderer
 *                                             // could in principle
 *                                             // target an offscreen
 *                                             // surface with no window
 *                                             // to swap
 *
 * Dependencies: Core and Graphics Context (for its GL function pointers
 * and OpenGL types - eisenfront/renderer.h intentionally exposes OpenGL
 * for the same reason graphics_context.h does).
 */
#ifndef EISENFRONT_RENDERER_H
#define EISENFRONT_RENDERER_H

#include "core.h"
#include "graphics_context.h"

typedef struct RendererDesc {
    uint32_t max_commands_per_frame; /* 0 defaults to 4096 */
} RendererDesc;

ENGINE_API RendererDesc renderer_desc_default(void);

typedef struct Renderer Renderer;

ENGINE_API Result renderer_create(GraphicsContext *context, const RendererDesc *desc,
                                   Renderer **out_renderer);
ENGINE_API void   renderer_destroy(Renderer *renderer);

ENGINE_API void renderer_begin_frame(Renderer *renderer);
ENGINE_API void renderer_end_frame(Renderer *renderer);

typedef enum ClearFlags {
    CLEAR_FLAG_COLOR = 1u << 0,
    CLEAR_FLAG_DEPTH = 1u << 1,
    CLEAR_FLAG_STENCIL = 1u << 2,
} ClearFlags;

typedef struct ClearParams {
    uint32_t flags; /* bitwise-OR of ClearFlags */
    float    red, green, blue, alpha;
    float    depth;
    int32_t  stencil;
} ClearParams;

ENGINE_API ClearParams clear_params_default(void);
ENGINE_API void        renderer_clear(Renderer *renderer, const ClearParams *params);

ENGINE_API void renderer_set_viewport(Renderer *renderer, int32_t x, int32_t y, int32_t width,
                                       int32_t height);

ENGINE_API void renderer_set_wireframe(Renderer *renderer, bool enabled);
ENGINE_API bool renderer_get_wireframe(const Renderer *renderer);

typedef enum PrimitiveTopology {
    PRIMITIVE_TOPOLOGY_TRIANGLES = 0,
    PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    PRIMITIVE_TOPOLOGY_LINES,
    PRIMITIVE_TOPOLOGY_LINE_STRIP,
    PRIMITIVE_TOPOLOGY_POINTS,
} PrimitiveTopology;

/* A fully self-contained draw unit: everything needed to issue one
 * glDraw* call. vertex_array and shader_program are raw GL object names
 * (whatever created them - a future Mesh/Shader module, or a caller
 * doing it by hand); Renderer never creates or owns GL objects itself,
 * only the queue and the frame around it. If indexed is true,
 * vertex_array's element array buffer binding (part of VAO state)
 * supplies the indices; index_count/vertex_count select how many to
 * draw either way. sort_key is caller-defined (a typical choice is
 * shader_program in the high bits, then depth) and only affects the
 * order commands execute in, to minimize state changes - it never
 * changes what gets drawn. */
typedef struct RenderCommand {
    uint32_t          vertex_array;
    uint32_t          shader_program;
    PrimitiveTopology topology;
    bool              indexed;
    uint32_t          index_count;
    uint32_t          vertex_count;
    uint32_t          instance_count; /* 0 or 1 both mean "not instanced" */
    uint64_t          sort_key;
} RenderCommand;

ENGINE_API Result renderer_submit(Renderer *renderer, const RenderCommand *command);

typedef struct RendererStats {
    uint32_t draw_calls;
    uint32_t triangles;
    uint32_t state_changes;
    uint64_t frame_count;
} RendererStats;

ENGINE_API void renderer_get_stats(const Renderer *renderer, RendererStats *out_stats);

#endif /* EISENFRONT_RENDERER_H */
