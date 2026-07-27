/*
 * Outpost - the Eisenfront engine's vertical slice (see
 * Docs/ and the plan this was built from for what it's proving).
 *
 * Phase 2: FPS freecam (no collision yet), a textured ground plane and
 * one lit reference cube, one directional sun light, distance fog.
 *
 * Renderer/Material integration note (discovered building this phase,
 * not assumed going in): renderer_end_frame() defers every queued
 * renderer_submit()ted draw to one sorted batch, but glUniform values
 * and texture/UBO bindings are persistent GL state set immediately by
 * material_bind()/shader_set_uniform_*() - not captured per-command.
 * Two submits sharing a shader/material but needing different uniforms
 * or textures would silently draw with whichever was bound *last* by
 * the time end_frame() actually issues the GL draw calls. The fix used
 * throughout this slice: renderer_begin_frame()+bind+submit+
 * renderer_end_frame() once per distinct material/GL-state group,
 * flushing immediately while that group's state is still correctly
 * bound, rather than queuing the whole frame before one end_frame().
 * Per-object varying data (this frame: none yet - ground and cube are
 * both baked to world space) must instead live in vertex data or an
 * instance buffer; see lit.vert's own file header comment.
 */
#include "eisenfront/core.h"
#include "eisenfront/graphics_context.h"
#include "eisenfront/input.h"
#include "eisenfront/lighting.h"
#include "eisenfront/material.h"
#include "eisenfront/mesh.h"
#include "eisenfront/renderer.h"
#include "eisenfront/shader.h"
#include "eisenfront/texture.h"
#include "eisenfront/window.h"

#include "eisenfront/audio.h"

#include "ground_texture.h"
#include "player.h"
#include "primitives.h"
#include "screenshot.h"

#include <stdlib.h>

/* Editor is not wired in yet - see Phase 8. Both input_init() and
 * editor_init() claim Window's single raw-event callback slot (there
 * is no built-in chaining - see window.h/editor.h), so bringing Editor
 * in here would silently break WASD/mouse-look the moment
 * editor_init() overwrites Input's registration. Phase 8 fixes this at
 * the engine level (a public "process one raw event" entry point on
 * both Input and Editor) rather than papering over it here. */

#define OUTPOST_LOG_CATEGORY "outpost"

#ifndef OUTPOST_ASSETS_DIR
#define OUTPOST_ASSETS_DIR "Assets"
#endif

/* Hazy overcast morning, matched between the clear color, fog color and
 * (from Phase 3 onward) the sky dome's horizon color, so the whole
 * scene reads as one atmosphere rather than a colored fog wall against
 * a mismatched background. */
static const float OUTPOST_SKY_HORIZON_COLOR[3] = {0.62f, 0.66f, 0.68f};
static const float OUTPOST_FOG_DENSITY = 0.012f;

typedef struct DrawableGroup {
    ShaderProgram *shader;
    Material      *material;
    Mesh          *mesh;
} DrawableGroup;

/* See the file header comment: flushing immediately, once per group,
 * is what keeps each group's material/uniform state correct at the
 * moment its draw actually executes inside renderer_end_frame(). */
static void draw_group(Renderer *renderer, const DrawableGroup *group, mat4 view, mat4 proj,
                        vec3 camera_position) {
    renderer_begin_frame(renderer);

    material_bind(group->material);
    shader_set_uniform_mat4(group->shader, "uView", (const float *)view);
    shader_set_uniform_mat4(group->shader, "uProj", (const float *)proj);
    shader_set_uniform_3f(group->shader, "uCameraPos", camera_position[0], camera_position[1],
                            camera_position[2]);
    shader_set_uniform_3f(group->shader, "uFogColor", OUTPOST_SKY_HORIZON_COLOR[0],
                            OUTPOST_SKY_HORIZON_COLOR[1], OUTPOST_SKY_HORIZON_COLOR[2]);
    shader_set_uniform_1f(group->shader, "uFogDensity", OUTPOST_FOG_DENSITY);

    const RenderCommand command = {
        .vertex_array = mesh_get_vertex_array_handle(group->mesh),
        .shader_program = shader_program_get_gl_handle(group->shader),
        .topology = PRIMITIVE_TOPOLOGY_TRIANGLES,
        .indexed = mesh_is_indexed(group->mesh),
        .index_count = mesh_get_index_count(group->mesh),
        .vertex_count = mesh_get_vertex_count(group->mesh),
        .instance_count = 1,
        .sort_key = 0,
    };
    renderer_submit(renderer, &command);
    renderer_end_frame(renderer);
}

int main(void) {
    log_init(LOG_LEVEL_INFO);
    LOG_INFO(OUTPOST_LOG_CATEGORY, "Outpost starting");

    if (window_init() != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "window_init failed");
        return 1;
    }
    if (input_init() != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "input_init failed");
        window_shutdown();
        return 1;
    }

    const WindowDesc window_desc = {
        .title = "Eisenfront - Outpost",
        .width = 1280,
        .height = 720,
        .mode = WINDOW_MODE_WINDOWED,
        .resizable = true,
        .monitor_index = -1,
    };
    Window *window = nullptr;
    if (window_create(&window_desc, &window) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "window_create failed");
        input_shutdown();
        window_shutdown();
        return 1;
    }
    input_set_mouse_relative_mode(window, true);

    GraphicsContextDesc context_desc = graphics_context_desc_default();
    context_desc.debug = true;
    context_desc.vsync = VSYNC_ON;
    GraphicsContext *context = nullptr;
    if (graphics_context_create(window, &context_desc, &context) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "graphics_context_create failed");
        window_destroy(window);
        input_shutdown();
        window_shutdown();
        return 1;
    }

    Renderer *renderer = nullptr;
    if (renderer_create(context, nullptr, &renderer) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "renderer_create failed");
        graphics_context_destroy(context);
        window_destroy(window);
        input_shutdown();
        window_shutdown();
        return 1;
    }

    AudioEngineDesc audio_desc = audio_engine_desc_default();
    AudioEngine    *audio = nullptr;
    if (audio_engine_create(&audio_desc, &audio) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "audio_engine_create failed");
        renderer_destroy(renderer);
        graphics_context_destroy(context);
        window_destroy(window);
        input_shutdown();
        window_shutdown();
        return 1;
    }

    if (shader_system_init() != RESULT_OK || texture_system_init() != RESULT_OK ||
        material_system_init() != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "content system init failed");
        audio_engine_destroy(audio);
        renderer_destroy(renderer);
        graphics_context_destroy(context);
        window_destroy(window);
        input_shutdown();
        window_shutdown();
        return 1;
    }

    const ShaderProgramDesc lit_shader_desc = {
        .vertex_path = OUTPOST_ASSETS_DIR "/shaders/lit.vert",
        .fragment_path = OUTPOST_ASSETS_DIR "/shaders/lit.frag",
        .geometry_path = nullptr,
    };
    ShaderProgram *lit_shader = nullptr;
    if (shader_program_load(&lit_shader_desc, &lit_shader) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "failed to load lit shader");
        material_system_shutdown();
        texture_system_shutdown();
        shader_system_shutdown();
        audio_engine_destroy(audio);
        renderer_destroy(renderer);
        graphics_context_destroy(context);
        window_destroy(window);
        input_shutdown();
        window_shutdown();
        return 1;
    }

    Texture *ground_diffuse = nullptr;
    if (ground_texture_create(512, 1337u, &ground_diffuse) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "failed to generate ground texture");
        shader_program_release(lit_shader);
        material_system_shutdown();
        texture_system_shutdown();
        shader_system_shutdown();
        audio_engine_destroy(audio);
        renderer_destroy(renderer);
        graphics_context_destroy(context);
        window_destroy(window);
        input_shutdown();
        window_shutdown();
        return 1;
    }

    MaterialParams ground_params = material_params_default();
    const MaterialDesc ground_material_desc = {
        .shader = lit_shader,
        .textures = {.diffuse = ground_diffuse},
        .params = ground_params,
    };
    Material *ground_material = nullptr;
    material_create(&ground_material_desc, &ground_material);

    MaterialParams cube_params = material_params_default();
    cube_params.base_color[0] = 0.5f;
    cube_params.base_color[1] = 0.5f;
    cube_params.base_color[2] = 0.52f;
    const MaterialDesc cube_material_desc = {
        .shader = lit_shader,
        .textures = {0},
        .params = cube_params,
    };
    Material *cube_material = nullptr;
    material_create(&cube_material_desc, &cube_material);

    Mesh *ground_mesh = nullptr;
    primitive_create_ground_plane((vec3){0.0f, 0.0f, 0.0f}, 100.0f, 100.0f, 0.25f, &ground_mesh);

    Mesh *cube_mesh = nullptr;
    primitive_create_box((vec3){6.0f, 1.0f, 0.0f}, (vec3){1.0f, 1.0f, 1.0f}, 1.0f, &cube_mesh);

    LightingEnvironment *lighting = nullptr;
    lighting_environment_create(&lighting);
    vec3 sun_direction = {-0.35f, -0.85f, -0.4f};
    glm_vec3_normalize(sun_direction);
    const DirectionalLight sun = {
        .direction = {sun_direction[0], sun_direction[1], sun_direction[2]},
        .color = {1.0f, 0.96f, 0.88f},
        .intensity = 3.2f,
    };
    lighting_set_directional(lighting, &sun);
    lighting_upload(lighting);

    Player player = player_create((vec3){0.0f, 1.75f, 0.0f}, (float)window_desc.width / (float)window_desc.height);

    const DrawableGroup ground_group = {.shader = lit_shader, .material = ground_material, .mesh = ground_mesh};
    const DrawableGroup cube_group = {.shader = lit_shader, .material = cube_material, .mesh = cube_mesh};

    FrameClock clock;
    frame_clock_init(&clock);

    /* Headless verification hook: this sandbox has no real display or
     * input device to close the window by hand. OUTPOST_MAX_FRAMES, if
     * set, bounds the loop so the frame lifecycle (every module's
     * per-frame call, every destroy on the way out) can be proven to
     * run cleanly without a human present. Unset in a normal play
     * session - the window's own close button and ESC still work. This
     * becomes the seed of the --smoke-test mode in the final pass. */
    const char *max_frames_env = getenv("OUTPOST_MAX_FRAMES");
    const long  max_frames = (max_frames_env != nullptr) ? strtol(max_frames_env, nullptr, 10) : 0;
    long        frame_count = 0;

    /* Same rationale: captures the final rendered frame to a PNG for a
     * human to review, since this sandbox has no display of its own. */
    const char *screenshot_path = getenv("OUTPOST_SCREENSHOT_PATH");

    LOG_INFO(OUTPOST_LOG_CATEGORY, "Entering frame loop");
    while (!window_should_close(window)) {
        input_new_frame();
        window_poll_events();

        if (input_key_pressed(KEY_ESCAPE)) {
            window_request_close(window);
        }

        frame_clock_tick(&clock);
        const float delta_seconds = (float)frame_clock_delta_seconds(&clock);

        player_update_freecam(&player, delta_seconds);

        int32_t width, height;
        window_get_size_in_pixels(window, &width, &height);
        if (height > 0) {
            camera_set_aspect_ratio(&player.camera, (float)width / (float)height);
        }

        mat4 view, proj;
        camera_get_view_matrix(&player.camera, view);
        camera_get_projection_matrix(&player.camera, proj);

        lighting_bind(lighting);

        renderer_begin_frame(renderer);
        renderer_set_viewport(renderer, 0, 0, width, height);
        const ClearParams clear = {
            .flags = CLEAR_FLAG_COLOR | CLEAR_FLAG_DEPTH,
            .red = OUTPOST_SKY_HORIZON_COLOR[0],
            .green = OUTPOST_SKY_HORIZON_COLOR[1],
            .blue = OUTPOST_SKY_HORIZON_COLOR[2],
            .alpha = 1.0f,
            .depth = 1.0f,
            .stencil = 0,
        };
        renderer_clear(renderer, &clear);
        renderer_end_frame(renderer);

        draw_group(renderer, &ground_group, view, proj, player.camera.position);
        draw_group(renderer, &cube_group, view, proj, player.camera.position);

        if (screenshot_path != nullptr) {
            screenshot_capture(width, height, screenshot_path);
        }

        audio_engine_update(audio);
        graphics_context_swap_buffers(context);

        frame_count += 1;
        if (max_frames > 0 && frame_count >= max_frames) {
            window_request_close(window);
        }
    }

    LOG_INFO(OUTPOST_LOG_CATEGORY, "Shutting down");

    lighting_environment_destroy(lighting);
    mesh_destroy(cube_mesh);
    mesh_destroy(ground_mesh);
    material_release(cube_material);
    material_release(ground_material);
    texture_release(ground_diffuse);
    shader_program_release(lit_shader);
    material_system_shutdown();
    texture_system_shutdown();
    shader_system_shutdown();
    audio_engine_destroy(audio);
    renderer_destroy(renderer);
    graphics_context_destroy(context);
    window_destroy(window);
    input_shutdown();
    window_shutdown();
    log_shutdown();
    return 0;
}
