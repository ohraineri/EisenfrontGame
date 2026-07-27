/*
 * Outpost - the Eisenfront engine's vertical slice (see
 * Docs/ and the plan this was built from for what it's proving).
 *
 * Phase 2: FPS freecam (no collision yet), a textured ground plane and
 * one lit reference cube, one directional sun light, distance fog.
 * Phase 3: procedural sky dome, single-light shadow mapping, and an
 * HDR bloom/tonemap/gamma postprocess pass wrapping the whole frame.
 * Phase 4: the outpost itself (outpost_scene.c) - perimeter barriers,
 * a watchtower, two structures and scattered props, built through
 * Scene's node hierarchy with matching static Physics bodies. The
 * Phase 2 reference cube is gone, superseded by real content.
 * Phase 5: the freecam is gone - player.c now drives a real
 * CharacterController (collide-and-slide against every static body
 * above, plus Game-managed gravity/jump; see player.c's file header
 * comment on why gravity isn't automatic).
 * Phase 6: AI soldiers (ai_soldier.c) - the repo's first real ECS
 * gameplay layer, four capsule-collision soldiers patrolling fixed
 * loops, kept in sync with kinematic Physics bodies the player
 * collides against. First use of the per-object uModel path in
 * lit.vert (everything before this was pre-baked to world space).
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
#include "eisenfront/framebuffer.h"
#include "eisenfront/graphics_context.h"
#include "eisenfront/input.h"
#include "eisenfront/lighting.h"
#include "eisenfront/material.h"
#include "eisenfront/mesh.h"
#include "eisenfront/physics.h"
#include "eisenfront/postprocess.h"
#include "eisenfront/renderer.h"
#include "eisenfront/shader.h"
#include "eisenfront/shadow.h"
#include "eisenfront/texture.h"
#include "eisenfront/window.h"

#include "eisenfront/audio.h"

#include "ai_soldier.h"
#include "ground_texture.h"
#include "outpost_scene.h"
#include "player.h"
#include "primitives.h"
#include "screenshot.h"
#include "sky.h"

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
    mat4           model; /* identity for pre-baked-to-world-space static geometry */
} DrawableGroup;

/* See the file header comment: flushing immediately, once per group,
 * is what keeps each group's material/uniform state correct at the
 * moment its draw actually executes inside renderer_end_frame(). */
static void draw_group(Renderer *renderer, const DrawableGroup *group, mat4 view, mat4 proj,
                        mat4 light_space_matrix, uint32_t shadow_depth_texture,
                        vec3 camera_position) {
    renderer_begin_frame(renderer);

    material_bind(group->material);
    shader_set_uniform_mat4(group->shader, "uView", (const float *)view);
    shader_set_uniform_mat4(group->shader, "uProj", (const float *)proj);
    shader_set_uniform_mat4(group->shader, "uModel", (const float *)group->model);
    shader_set_uniform_mat4(group->shader, "uLightSpaceMatrix", (const float *)light_space_matrix);
    shader_set_uniform_3f(group->shader, "uCameraPos", camera_position[0], camera_position[1],
                            camera_position[2]);
    shader_set_uniform_3f(group->shader, "uFogColor", OUTPOST_SKY_HORIZON_COLOR[0],
                            OUTPOST_SKY_HORIZON_COLOR[1], OUTPOST_SKY_HORIZON_COLOR[2]);
    shader_set_uniform_1f(group->shader, "uFogDensity", OUTPOST_FOG_DENSITY);
    /* Not one of Material's own six slots - bound directly, matching
     * lit.frag's explicit `layout(binding = 6)`. */
    glBindTextureUnit(6, shadow_depth_texture);

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

    /* Neither GraphicsContext nor Renderer ever enable GL_DEPTH_TEST for
     * the main scene pass - shadow.c only enables it for its own
     * depth-only FBO, postprocess.c only assumes/restores whatever
     * state it found. Depth testing is left as the application's
     * responsibility (a reasonable division - not every use of
     * Renderer needs a depth buffer), so it must be turned on here,
     * once, before anything with overlapping geometry is drawn. */
    glEnable(GL_DEPTH_TEST);

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

    Mesh *ground_mesh = nullptr;
    primitive_create_ground_plane((vec3){0.0f, 0.0f, 0.0f}, 100.0f, 100.0f, 0.25f, &ground_mesh);

    PhysicsWorldDesc physics_world_desc = physics_world_desc_default();
    physics_world_desc.max_bodies = 256u;
    PhysicsWorld *physics_world = nullptr;
    physics_world_create(&physics_world_desc, &physics_world);

    /* Ground itself is a static body too, matching the visual plane -
     * everything in the outpost needs something solid underfoot. */
    RigidBodyDesc ground_body_desc = rigid_body_desc_default();
    ground_body_desc.type = BODY_TYPE_STATIC;
    ground_body_desc.shape = shape_box((vec3){100.0f, 0.5f, 100.0f});
    ground_body_desc.position[1] = -0.5f;
    BodyId ground_body = PHYSICS_INVALID_BODY_ID;
    rigid_body_create(physics_world, &ground_body_desc, &ground_body);

    OutpostLevel outpost_level = {0};
    if (outpost_level_create(lit_shader, physics_world, &outpost_level) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "failed to build the outpost level");
        return 1;
    }

    AiSoldierSquad soldier_squad = {0};
    if (ai_soldier_squad_create(lit_shader, physics_world, &soldier_squad) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "failed to create the AI soldier squad");
        return 1;
    }

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

    ShadowMap *shadow_map = nullptr;
    shadow_map_create(2048u, &shadow_map);
    shadow_map_set_light(shadow_map, sun_direction, (vec3){0.0f, 0.0f, -3.0f}, 35.0f);

    /* Fixed to the window's initial size; resizing the window does not
     * currently recreate this at the new resolution (a known
     * limitation, not core to what this phase is proving). */
    PostProcessDesc postprocess_desc =
        postprocess_desc_default((uint32_t)window_desc.width, (uint32_t)window_desc.height);
    postprocess_desc.enable_bloom = true;
    postprocess_desc.exposure = 1.1f;
    PostProcess *postprocess = nullptr;
    postprocess_create(&postprocess_desc, &postprocess);

    vec3 sky_horizon_color = {OUTPOST_SKY_HORIZON_COLOR[0], OUTPOST_SKY_HORIZON_COLOR[1],
                               OUTPOST_SKY_HORIZON_COLOR[2]};
    vec3 sky_zenith_color = {0.35f, 0.42f, 0.52f};
    vec3 sky_sun_color = {1.0f, 0.92f, 0.75f};
    Sky  sky;
    if (sky_create(OUTPOST_ASSETS_DIR, sky_horizon_color, sky_zenith_color, sun_direction, sky_sun_color,
                   &sky) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "failed to load sky shader");
        return 1;
    }

    /* Spawn south of the checkpoint, facing north through the entrance
     * gap toward the watchtower and the compound beyond - the natural
     * first approach to a defended position, not dropped inside it.
     * Y is the capsule CENTER (radius + half_height above the ground),
     * not eye height - player_create() derives eye height from it. */
    Player player;
    if (player_create(physics_world, (vec3){0.0f, 0.9f, -32.0f},
                       (float)window_desc.width / (float)window_desc.height, &player) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "failed to create the player character controller");
        return 1;
    }
    player.camera.yaw_radians = glm_rad(90.0f);

    DrawableGroup ground_group = {.shader = lit_shader, .material = ground_material, .mesh = ground_mesh};
    glm_mat4_identity(ground_group.model);

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
        /* Clamped rather than raw wall-clock delta: a debugger pause, a
         * slow first frame (shader compiles above), or - as measured in
         * this sandbox - the offscreen SDL driver's own swap timing can
         * all produce a huge single-frame delta. Fed straight into
         * gravity integration that compounds every frame, an
         * unclamped delta explodes the player's vertical velocity in
         * one step (observed here as the character launching through
         * the roof on the very first physics-driven frame). Standard
         * fix: cap the simulated step regardless of real elapsed time -
         * the game runs in slow motion during a real hitch instead of
         * blowing up, which is the correct tradeoff. */
        float delta_seconds = (float)frame_clock_delta_seconds(&clock);
        if (delta_seconds > 1.0f / 30.0f) {
            delta_seconds = 1.0f / 30.0f;
        }

        /* Every body here is either static or a kinematic soldier moved
         * by direct rigid_body_set_transform() (see ai_soldier.c) - no
         * dynamic body ever needs physics_world_step()'s own force/
         * gravity integration in this scene, and the player's
         * CharacterController sweeps live body positions itself,
         * independent of this call. Still stepped every frame, as any
         * real game would, so it's already wired for the moment
         * something dynamic does exist. */
        physics_world_step(physics_world, delta_seconds);
        ai_soldier_squad_update(&soldier_squad, delta_seconds);
        player_update(&player, delta_seconds);

        int32_t width, height;
        window_get_size_in_pixels(window, &width, &height);
        if (height > 0) {
            camera_set_aspect_ratio(&player.camera, (float)width / (float)height);
        }

        mat4 view, proj;
        camera_get_view_matrix(&player.camera, view);
        camera_get_projection_matrix(&player.camera, proj);

        mat4 light_space_matrix;
        shadow_map_get_light_space_matrix(shadow_map, light_space_matrix);

        mat4 identity_model;
        glm_mat4_identity(identity_model);
        shadow_map_begin(shadow_map);
        for (uint32_t i = 0; i < outpost_level.object_count; ++i) {
            const StaticObject *object = &outpost_level.objects[i];
            shadow_map_draw(shadow_map, identity_model, mesh_get_vertex_array_handle(object->mesh),
                             mesh_get_vertex_count(object->mesh), mesh_get_index_count(object->mesh));
        }
        for (uint32_t i = 0; i < soldier_squad.count; ++i) {
            mat4 soldier_model;
            ai_soldier_get_world_matrix(&soldier_squad, i, soldier_model);
            shadow_map_draw(shadow_map, soldier_model, mesh_get_vertex_array_handle(soldier_squad.mesh),
                             mesh_get_vertex_count(soldier_squad.mesh),
                             mesh_get_index_count(soldier_squad.mesh));
        }
        /* Binds the HDR scene framebuffer postprocess_resolve_and_apply()
         * later reads from - replaces the plain framebuffer_bind_default()
         * a shadow-only frame would use. */
        postprocess_begin_scene(postprocess);

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

        const uint32_t shadow_depth_texture = shadow_map_get_depth_texture(shadow_map);
        draw_group(renderer, &ground_group, view, proj, light_space_matrix, shadow_depth_texture,
                   player.camera.position);
        for (uint32_t i = 0; i < outpost_level.object_count; ++i) {
            const StaticObject *object = &outpost_level.objects[i];
            DrawableGroup group = {
                .shader = lit_shader, .material = object->material, .mesh = object->mesh};
            glm_mat4_identity(group.model);
            draw_group(renderer, &group, view, proj, light_space_matrix, shadow_depth_texture,
                       player.camera.position);
        }
        for (uint32_t i = 0; i < soldier_squad.count; ++i) {
            DrawableGroup group = {
                .shader = lit_shader, .material = soldier_squad.material, .mesh = soldier_squad.mesh};
            ai_soldier_get_world_matrix(&soldier_squad, i, group.model);
            draw_group(renderer, &group, view, proj, light_space_matrix, shadow_depth_texture,
                       player.camera.position);
        }
        /* Sky must draw last - see sky.vert's file header comment on
         * the depth trick this relies on. */
        sky_draw(renderer, &sky, view, proj);

        framebuffer_bind_default((uint32_t)width, (uint32_t)height);
        postprocess_resolve_and_apply(postprocess, (uint32_t)width, (uint32_t)height);

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

    sky_destroy(&sky);
    postprocess_destroy(postprocess);
    shadow_map_destroy(shadow_map);
    lighting_environment_destroy(lighting);
    player_destroy(&player);
    ai_soldier_squad_destroy(&soldier_squad);
    outpost_level_destroy(&outpost_level, physics_world);
    rigid_body_destroy(physics_world, ground_body);
    physics_world_destroy(physics_world);
    mesh_destroy(ground_mesh);
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
