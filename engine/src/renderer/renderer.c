#include "eisenfront/renderer.h"

#include <stdlib.h>

#define RENDERER_LOG_CATEGORY "renderer"
#define RENDERER_DEFAULT_MAX_COMMANDS 4096u

struct Renderer {
    GraphicsContext *context;
    RenderCommand    *commands;
    uint32_t          command_capacity;
    uint32_t          command_count;
    bool              wireframe_enabled;
    RendererStats     stats;
};

RendererDesc renderer_desc_default(void) {
    return (RendererDesc){.max_commands_per_frame = RENDERER_DEFAULT_MAX_COMMANDS};
}

Result renderer_create(GraphicsContext *context, const RendererDesc *desc,
                        Renderer **out_renderer) {
    if (context == nullptr || out_renderer == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const RendererDesc effective = (desc != nullptr) ? *desc : renderer_desc_default();
    const uint32_t capacity =
        effective.max_commands_per_frame > 0 ? effective.max_commands_per_frame : RENDERER_DEFAULT_MAX_COMMANDS;

    Renderer *renderer = malloc(sizeof(*renderer));
    if (renderer == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    renderer->commands = malloc(sizeof(RenderCommand) * capacity);
    if (renderer->commands == nullptr) {
        free(renderer);
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    renderer->context = context;
    renderer->command_capacity = capacity;
    renderer->command_count = 0;
    renderer->wireframe_enabled = false;
    renderer->stats = (RendererStats){0};

    LOG_INFO(RENDERER_LOG_CATEGORY, "Renderer created (capacity: %u commands/frame)", capacity);

    *out_renderer = renderer;
    return RESULT_OK;
}

void renderer_destroy(Renderer *renderer) {
    if (renderer == nullptr) {
        return;
    }
    free(renderer->commands);
    free(renderer);
}

void renderer_begin_frame(Renderer *renderer) {
    ASSERT(renderer != nullptr);
    if (renderer == nullptr) {
        return;
    }
    renderer->command_count = 0;
    renderer->stats.draw_calls = 0;
    renderer->stats.triangles = 0;
    renderer->stats.state_changes = 0;
}

ClearParams clear_params_default(void) {
    return (ClearParams){
        .flags = CLEAR_FLAG_COLOR | CLEAR_FLAG_DEPTH,
        .red = 0.0f,
        .green = 0.0f,
        .blue = 0.0f,
        .alpha = 1.0f,
        .depth = 1.0f,
        .stencil = 0,
    };
}

void renderer_clear(Renderer *renderer, const ClearParams *params) {
    ASSERT(renderer != nullptr);
    if (renderer == nullptr) {
        return;
    }
    const ClearParams effective = (params != nullptr) ? *params : clear_params_default();

    GLbitfield mask = 0;
    if (effective.flags & CLEAR_FLAG_COLOR) {
        glClearColor(effective.red, effective.green, effective.blue, effective.alpha);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (effective.flags & CLEAR_FLAG_DEPTH) {
        glClearDepthf(effective.depth);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    if (effective.flags & CLEAR_FLAG_STENCIL) {
        glClearStencil(effective.stencil);
        mask |= GL_STENCIL_BUFFER_BIT;
    }
    if (mask != 0) {
        glClear(mask);
    }
}

void renderer_set_viewport(Renderer *renderer, int32_t x, int32_t y, int32_t width,
                            int32_t height) {
    ASSERT(renderer != nullptr);
    (void)renderer; /* glViewport is context-global GL state; renderer kept for API symmetry */
    glViewport(x, y, width, height);
}

void renderer_set_wireframe(Renderer *renderer, bool enabled) {
    ASSERT(renderer != nullptr);
    if (renderer == nullptr) {
        return;
    }
    glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL);
    renderer->wireframe_enabled = enabled;
}

bool renderer_get_wireframe(const Renderer *renderer) {
    ASSERT(renderer != nullptr);
    return renderer != nullptr ? renderer->wireframe_enabled : false;
}

Result renderer_submit(Renderer *renderer, const RenderCommand *command) {
    ASSERT(renderer != nullptr);
    ASSERT(command != nullptr);
    if (renderer == nullptr || command == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (renderer->command_count >= renderer->command_capacity) {
        return RESULT_ERROR_CAPACITY_EXCEEDED;
    }

    renderer->commands[renderer->command_count] = *command;
    renderer->command_count += 1;
    return RESULT_OK;
}

static int compare_render_commands(const void *lhs, const void *rhs) {
    const uint64_t left_key = ((const RenderCommand *)lhs)->sort_key;
    const uint64_t right_key = ((const RenderCommand *)rhs)->sort_key;
    if (left_key < right_key) {
        return -1;
    }
    if (left_key > right_key) {
        return 1;
    }
    return 0;
}

static GLenum gl_mode_for_topology(PrimitiveTopology topology) {
    switch (topology) {
        case PRIMITIVE_TOPOLOGY_TRIANGLES:
            return GL_TRIANGLES;
        case PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
            return GL_TRIANGLE_STRIP;
        case PRIMITIVE_TOPOLOGY_LINES:
            return GL_LINES;
        case PRIMITIVE_TOPOLOGY_LINE_STRIP:
            return GL_LINE_STRIP;
        case PRIMITIVE_TOPOLOGY_POINTS:
        default:
            return GL_POINTS;
    }
}

static uint32_t triangle_count_for_command(const RenderCommand *command) {
    const uint32_t count = command->indexed ? command->index_count : command->vertex_count;
    const uint32_t instances = command->instance_count > 0 ? command->instance_count : 1;

    uint32_t triangles_per_instance = 0;
    switch (command->topology) {
        case PRIMITIVE_TOPOLOGY_TRIANGLES:
            triangles_per_instance = count / 3;
            break;
        case PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
            triangles_per_instance = count >= 2 ? count - 2 : 0;
            break;
        default:
            triangles_per_instance = 0;
            break;
    }
    return triangles_per_instance * instances;
}

void renderer_end_frame(Renderer *renderer) {
    ASSERT(renderer != nullptr);
    if (renderer == nullptr) {
        return;
    }

    if (renderer->command_count > 1) {
        qsort(renderer->commands, renderer->command_count, sizeof(RenderCommand),
              compare_render_commands);
    }

    uint32_t bound_program = 0;
    uint32_t bound_vao = 0;
    bool     has_bound_program = false;
    bool     has_bound_vao = false;

    for (uint32_t i = 0; i < renderer->command_count; ++i) {
        const RenderCommand *command = &renderer->commands[i];

        if (!has_bound_program || command->shader_program != bound_program) {
            glUseProgram(command->shader_program);
            bound_program = command->shader_program;
            has_bound_program = true;
            renderer->stats.state_changes += 1;
        }
        if (!has_bound_vao || command->vertex_array != bound_vao) {
            glBindVertexArray(command->vertex_array);
            bound_vao = command->vertex_array;
            has_bound_vao = true;
            renderer->stats.state_changes += 1;
        }

        const GLenum   mode = gl_mode_for_topology(command->topology);
        const uint32_t instances = command->instance_count > 0 ? command->instance_count : 1;

        if (command->indexed) {
            if (instances > 1) {
                glDrawElementsInstanced(mode, (GLsizei)command->index_count, GL_UNSIGNED_INT,
                                         nullptr, (GLsizei)instances);
            } else {
                glDrawElements(mode, (GLsizei)command->index_count, GL_UNSIGNED_INT, nullptr);
            }
        } else {
            if (instances > 1) {
                glDrawArraysInstanced(mode, 0, (GLsizei)command->vertex_count, (GLsizei)instances);
            } else {
                glDrawArrays(mode, 0, (GLsizei)command->vertex_count);
            }
        }

        renderer->stats.draw_calls += 1;
        renderer->stats.triangles += triangle_count_for_command(command);
    }

    renderer->stats.frame_count += 1;
}

void renderer_get_stats(const Renderer *renderer, RendererStats *out_stats) {
    ASSERT(renderer != nullptr);
    ASSERT(out_stats != nullptr);
    if (renderer == nullptr || out_stats == nullptr) {
        return;
    }
    *out_stats = renderer->stats;
}
