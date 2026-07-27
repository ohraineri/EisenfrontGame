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
 * Phase 7: audio (game_audio.c) - looping non-spatial wind/ambience
 * plus spatial one-shot footsteps timed to the player's own horizontal
 * movement distance, from procedurally-synthesized placeholder .wav
 * files (see Tools/generate_audio.py).
 * Phase 8: the Editor debug overlay (debug_overlay.c), F1-toggled -
 * live RendererStats, object counts, a wireframe toggle, noclip, and
 * teleport-to-cursor. This is also where the Input/Editor raw-event
 * conflict flagged since Phase 1 actually gets resolved: both
 * input_init() and editor_init() still claim Window's single raw-event
 * slot internally, but now that input_process_raw_event() and
 * editor_process_raw_event() are exposed directly (an engine-level
 * fix), main() re-registers one small combined callback after both
 * _init() calls, so neither one silently loses events to the other.
 * Phase 9: OUTPOST_SMOKE_TEST_DIR - a scripted flythrough to five
 * vantage points around the outpost, one screenshot each, the final
 * pass's primary verification tool.
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
#include "camera_motion.h"
#include "game_audio.h"
#include "ground_texture.h"
#include "infantry_entity.h"
#include "infantry_input_command.h"
#include "outpost_scene.h"
#include "primitives.h"
#include "screenshot.h"
#include "sim_clock.h"
#include "sky.h"

#ifdef OUTPOST_HAS_EDITOR
#include "eisenfront/editor.h"

#include "debug_overlay.h"
#endif

#include <stdint.h>
#include <stdlib.h>

#ifdef OUTPOST_HAS_EDITOR
/* input_init() and editor_init() each claim Window's single raw-event
 * slot for themselves (see window.h/editor.h) - whichever _init() runs
 * last silently wins, breaking the other. Registered after both have
 * initialized, this one callback forwards every event to both of their
 * now-public handlers, so neither loses events to the other. */
static void combined_raw_event_handler(void *native_event, void *userdata) {
    input_process_raw_event(native_event, userdata);
    editor_process_raw_event(native_event, userdata);
}
#endif

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

/* Renderer's own renderer_get_stats() resets every renderer_begin_frame()
 * call, but every draw in this slice gets its own immediate flush (see
 * the file header comment) - many begin/end cycles per real frame, not
 * one. This accumulates across all of them; reset once per real frame,
 * read by the Phase 8 debug overlay. */
static RendererStats g_frame_stats;

static void accumulate_stats(Renderer *renderer) {
    RendererStats stats;
    renderer_get_stats(renderer, &stats);
    g_frame_stats.draw_calls += stats.draw_calls;
    g_frame_stats.triangles += stats.triangles;
    g_frame_stats.state_changes += stats.state_changes;
}

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
    accumulate_stats(renderer);
}

/* Bound as the userdata for InfantryGroundSurfaceLookupFn - bundles
 * exactly what outpost_surface_lookup() needs, so infantry_entity.c
 * itself never has to know about PhysicsWorld's raycast API or
 * OutpostLevel (see infantry_entity.h's file header comment on why the
 * lookup is a caller-supplied callback rather than a direct dependency). */
typedef struct OutpostSurfaceLookupContext {
    PhysicsWorld       *physics_world;
    const OutpostLevel *outpost_level;
    BodyId              ground_body;
} OutpostSurfaceLookupContext;

/* Called only when a foot-contact event is actually being created (see
 * infantry_entity.c's update_gait()), never once per fixed tick - a
 * short downward raycast from the exact contact point, not a stale
 * transform/camera position. */
static SurfaceType outpost_surface_lookup(void *userdata, vec3 world_position) {
    const OutpostSurfaceLookupContext *context = (const OutpostSurfaceLookupContext *)userdata;
    vec3       probe_origin = {world_position[0], world_position[1] + 0.1f, world_position[2]};
    vec3       probe_down = {0.0f, -1.0f, 0.0f};
    RaycastHit hit;
    if (!physics_raycast(context->physics_world, probe_origin, probe_down, 1.0f, COLLISION_LAYER_ALL,
                          &hit)) {
        return SURFACE_TYPE_SOIL;
    }
    if (hit.body == context->ground_body) {
        return SURFACE_TYPE_SOIL;
    }
    return outpost_level_surface_type_for_body(context->outpost_level, hit.body);
}

typedef struct SmokeTestWaypoint {
    const char *label;
    vec3        position;
    float       yaw_radians;
    float       pitch_radians;
} SmokeTestWaypoint;

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

#ifdef OUTPOST_HAS_EDITOR
    const bool editor_active = editor_init(window, context) == RESULT_OK;
    if (!editor_active) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "editor_init failed - continuing without the debug overlay");
    }
    /* Wins the raw-event slot back from whichever of input_init()/
     * editor_init() registered last - see combined_raw_event_handler's
     * own comment. */
    window_set_raw_event_callback(combined_raw_event_handler, nullptr);
    DebugOverlay debug_overlay = {0};
    /* Verification-only hook, same spirit as OUTPOST_MAX_FRAMES: no F1
     * keypress exists to press in a headless smoke test, so this lets
     * one confirm the overlay actually renders. */
    debug_overlay.visible = getenv("OUTPOST_FORCE_OVERLAY") != nullptr;
#endif

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
    const OutpostSurfaceLookupContext surface_lookup_context = {
        .physics_world = physics_world, .outpost_level = &outpost_level, .ground_body = ground_body};

    AiSoldierSquad soldier_squad = {0};
    if (ai_soldier_squad_create(lit_shader, physics_world, &soldier_squad) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "failed to create the AI soldier squad");
        return 1;
    }

    GameAudio game_audio = {0};
    if (game_audio_create(audio, OUTPOST_ASSETS_DIR, &game_audio) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "failed to load game audio");
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
     * not eye height - infantry_entity_create() derives eye height from it. */
    InfantryEntity player;
    if (infantry_entity_create(physics_world, (vec3){0.0f, 0.9f, -32.0f},
                                (float)window_desc.width / (float)window_desc.height,
                                &player) != RESULT_OK) {
        LOG_ERROR(OUTPOST_LOG_CATEGORY, "failed to create the player character controller");
        return 1;
    }
    /* Cached once - valid for the entity's lifetime (see
     * infantry_entity.h's file header comment) and used throughout the
     * frame loop below instead of an ecs_get_component() call per site. */
    InfantryViewComponent       *player_view = infantry_entity_get_view(&player);
    InfantryLocomotionComponent *player_locomotion = infantry_entity_get_locomotion(&player);
    InfantryTransformComponent  *player_transform = infantry_entity_get_transform(&player);
    player_view->camera.yaw_radians = glm_rad(90.0f);

    /* M11: presentation-only camera motion, entirely separate from
     * player_view->camera (the authoritative eye/aim transform) - see
     * camera_motion.h's file header comment. Intensities default to
     * 100%; debug_overlay.c exposes live sliders down to 0%. */
    CameraMotionState  player_camera_motion;
    CameraMotionConfig player_camera_motion_config = camera_motion_config_default();
    camera_motion_state_init(&player_camera_motion);

    DrawableGroup ground_group = {.shader = lit_shader, .material = ground_material, .mesh = ground_mesh};
    glm_mat4_identity(ground_group.model);

    /* Verification-only hook, same spirit as OUTPOST_MAX_FRAMES /
     * OUTPOST_SMOKE_TEST_DIR: a screenshot can't show sub-centimeter
     * camera motion, so this logs the numeric render-rate offset every
     * frame for a human (or this session) to review as a trace. */
    const bool trace_camera_motion = getenv("OUTPOST_TRACE_CAMERA_MOTION") != nullptr;

    FrameClock clock;
    frame_clock_init(&clock);
    SimClock sim_clock;
    sim_clock_init(&sim_clock);

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

    /* --smoke-test: a scripted flythrough to several vantage points
     * around the outpost, holding each long enough for physics/AI to
     * run for real before capturing it - the final verification pass's
     * primary tool, since there is no human here to walk around and
     * look. Overrides normal player input entirely while active
     * (camera and the CharacterController's own position are both
     * driven from the script, kept in sync exactly like noclip is). */
    const char *smoke_test_dir = getenv("OUTPOST_SMOKE_TEST_DIR");
    uint32_t    smoke_test_index = 0;
    uint32_t    smoke_test_frames_at_waypoint = 0;
#define SMOKE_TEST_HOLD_FRAMES 15u
    const SmokeTestWaypoint smoke_test_waypoints[] = {
        {"01_checkpoint_approach", {0.0f, 1.75f, -32.0f}, glm_rad(90.0f), 0.0f},
        /* Elevated, looking back north into the compound with a
         * downward pitch (positive pitch looks up - see camera.h) -
         * the whole point of standing on the platform. */
        {"02_watchtower_vantage", {0.0f, 6.15f, -16.0f}, glm_rad(90.0f), glm_rad(-25.0f)},
        {"03_command_building", {-8.0f, 1.75f, 16.0f}, glm_rad(-90.0f), 0.0f},
        {"04_supply_shed", {8.0f, 1.75f, 14.0f}, glm_rad(-90.0f), 0.0f},
        /* x=18, not the 12-16 range the east-perimeter soldier
         * patrols (see ai_soldier.c) - the original x=16 placed the
         * camera inside that soldier's own collision box. */
        {"05_east_perimeter", {18.0f, 1.75f, 0.0f}, glm_rad(90.0f), 0.0f},
    };
    const uint32_t smoke_test_waypoint_count = sizeof(smoke_test_waypoints) / sizeof(smoke_test_waypoints[0]);
    if (smoke_test_dir != nullptr) {
        LOG_INFO(OUTPOST_LOG_CATEGORY, "--smoke-test: %u waypoints, output to %s", smoke_test_waypoint_count,
                  smoke_test_dir);
    }

    LOG_INFO(OUTPOST_LOG_CATEGORY, "Entering frame loop");
    while (!window_should_close(window)) {
        input_new_frame();
        window_poll_events();
#ifdef OUTPOST_HAS_EDITOR
        if (editor_active) {
            editor_new_frame();
        }
#endif

        if (input_key_pressed(KEY_ESCAPE)) {
            window_request_close(window);
        }
#ifdef OUTPOST_HAS_EDITOR
        if (editor_active && input_key_pressed(KEY_F1)) {
            debug_overlay_toggle(&debug_overlay);
        }
#endif

        frame_clock_tick(&clock);
        const float real_delta_seconds = (float)frame_clock_delta_seconds(&clock);

        /* Look is sampled and applied once per render frame regardless
         * of how many fixed sim ticks that frame produces below -
         * applying the same mouse delta more than once per frame would
         * over-rotate (Infantry Controller spec 5.1/38.1: render-rate
         * variation must not change rotation speed). Movement/jump
         * fields ride along in the same command and are consumed once
         * per fixed tick instead - see the loop below. */
        InfantryInputCommand input_command = infantry_input_command_sample(real_delta_seconds);
        infantry_entity_apply_look(&player, &input_command);

        /* Fixed-timestep simulation: physics, AI and player movement
         * all advance in discrete SIM_FIXED_DELTA_SECONDS slices, zero
         * or more per render frame, so a debugger pause, a slow first
         * frame (shader compiles above), or the offscreen SDL driver's
         * own swap timing can't change acceleration, jump height, or
         * stopping distance the way a variable per-frame delta did
         * (previously observed here as the character launching through
         * the roof on the very first physics-driven frame at an
         * unclamped delta). sim_clock_accumulate() clamps the backlog
         * instead, so a real hitch runs in slow motion rather than
         * blowing up or spiraling into an ever-growing catch-up queue. */
        sim_clock_accumulate(&sim_clock, real_delta_seconds);
        while (sim_clock_consume_step(&sim_clock)) {
            /* Every body here is either static or a kinematic soldier
             * moved by direct rigid_body_set_transform() (see
             * ai_soldier.c) - no dynamic body ever needs
             * physics_world_step()'s own force/gravity integration in
             * this scene, and the player's CharacterController sweeps
             * live body positions itself, independent of this call.
             * Still stepped every tick, as any real game would, so
             * it's already wired for the moment something dynamic does
             * exist. */
            physics_world_step(physics_world, SIM_FIXED_DELTA_SECONDS);
            ai_soldier_squad_update(&soldier_squad, SIM_FIXED_DELTA_SECONDS);
            if (smoke_test_dir != nullptr && smoke_test_index < smoke_test_waypoint_count) {
                const SmokeTestWaypoint *waypoint = &smoke_test_waypoints[smoke_test_index];
                player_view->camera.position[0] = waypoint->position[0];
                player_view->camera.position[1] = waypoint->position[1];
                player_view->camera.position[2] = waypoint->position[2];
                player_view->camera.yaw_radians = waypoint->yaw_radians;
                player_view->camera.pitch_radians = waypoint->pitch_radians;
                vec3 controller_position = {waypoint->position[0],
                                             waypoint->position[1] - player_locomotion->eye_height_offset,
                                             waypoint->position[2]};
                character_controller_set_position(player_locomotion->controller, controller_position);
                glm_vec3_copy(controller_position, player_transform->position);
                /* Scripted placement, exactly like a teleport - never
                 * let a stale partial stride from before this waypoint
                 * jump surface as a phantom footstep, and never let
                 * camera motion interpolate across the jump either. */
                infantry_entity_reset_gait(&player);
                camera_motion_reset(&player_camera_motion);
            } else {
                infantry_entity_update_fixed(&player, &input_command, SIM_FIXED_DELTA_SECONDS,
                                              sim_clock_tick_index(&sim_clock), outpost_surface_lookup,
                                              (void *)&surface_lookup_context);
                /* jump_requested is a one-shot edge already consumed by
                 * this tick; clearing it stops a multi-tick catch-up
                 * frame from queuing a second jump from the same
                 * keypress. */
                input_command.jump_requested = false;

                /* Landing must enter camera_motion's CURRENT fixed-tick
                 * state on the same tick it's detected (not a render
                 * frame later, unlike the foot-contact queue below,
                 * which is fine draining at render-frame granularity) -
                 * a second single-dispatcher drain point, same
                 * ownership rule, different queue, different required
                 * timing. */
                InfantryFallLandingEvent landing_event;
                const bool                has_landing_event =
                    infantry_entity_pop_landing_event(&player, &landing_event);
                PlayerGaitPresentationSnapshot gait_snapshot;
                infantry_entity_get_gait_presentation_snapshot(&player, &gait_snapshot);
                camera_motion_update_fixed(&player_camera_motion, &player_camera_motion_config,
                                            gait_snapshot, player_locomotion->horizontal_velocity,
                                            player_transform->body_yaw_radians,
                                            has_landing_event ? &landing_event : nullptr,
                                            SIM_FIXED_DELTA_SECONDS);
            }
        }
        g_frame_stats = (RendererStats){0};

        {
            /* Single dispatcher: main.c owns draining locomotion's
             * foot-contact queue and fans the same immutable event out
             * to every registered consumer itself (audio today) - see
             * infantry_foot_contact_event.h's file header comment.
             * Future consumers (animation, AI perception, particles,
             * decals, networking, equipment audio) get added here, and
             * must never call infantry_entity_pop_foot_contact_event()
             * themselves. */
            InfantryFootContactEvent event;
            while (infantry_entity_pop_foot_contact_event(&player, &event)) {
                game_audio_play_footstep(audio, &game_audio, &event);
                LOG_INFO(OUTPOST_LOG_CATEGORY, "Footstep: %s foot, %s surface (tick %llu)",
                          event.foot == PLAYER_FOOT_LEFT ? "Left" : "Right",
                          surface_type_name(event.surface_type),
                          (unsigned long long)event.sim_tick_index);
            }
        }

        vec3 camera_forward, camera_up;
        camera_get_forward(&player_view->camera, camera_forward);
        camera_get_up(&player_view->camera, camera_up);
        audio_engine_set_listener(audio, player_view->camera.position, camera_forward, camera_up);

        int32_t width, height;
        window_get_size_in_pixels(window, &width, &height);
        if (height > 0) {
            camera_set_aspect_ratio(&player_view->camera, (float)width / (float)height);
        }

        /* Render-rate interpolation (never extrapolation past the
         * newest simulated tick): alpha is how far INTO the next fixed
         * tick real time has already accumulated, so the render frame
         * always sits between the last two actually-simulated states,
         * regardless of render frame rate. */
        const float camera_motion_alpha =
            sim_clock.accumulator_seconds / SIM_FIXED_DELTA_SECONDS; /* camera_motion clamps defensively too */
        vec3 camera_motion_local_offset;
        camera_motion_get_render_offset(&player_camera_motion, camera_motion_alpha, camera_motion_local_offset);
        if (trace_camera_motion) {
            LOG_INFO(OUTPOST_LOG_CATEGORY, "CameraMotion: right=%.5f up=%.5f fwd=%.5f alpha=%.3f",
                      camera_motion_local_offset[0], camera_motion_local_offset[1],
                      camera_motion_local_offset[2], camera_motion_alpha);
        }

        /* Applied to a LOCAL COPY only - player_view->camera (the
         * authoritative eye/aim transform movement, weapon direction,
         * and network state all read) is never modified. Converted
         * using the CAMERA's own basis (view-relative: what you're
         * looking at), not the body's - body_yaw_radians is used
         * inside camera_motion.c only to derive a stable, view-
         * independent realized-acceleration direction for inertia, not
         * for this final placement. */
        Camera render_camera = player_view->camera;
        vec3   render_camera_forward, render_camera_right, render_camera_up;
        camera_get_forward(&render_camera, render_camera_forward);
        camera_get_right(&render_camera, render_camera_right);
        camera_get_up(&render_camera, render_camera_up);
        glm_vec3_muladds(render_camera_right, camera_motion_local_offset[0], render_camera.position);
        glm_vec3_muladds(render_camera_up, camera_motion_local_offset[1], render_camera.position);
        glm_vec3_muladds(render_camera_forward, camera_motion_local_offset[2], render_camera.position);

        mat4 view, proj;
        camera_get_view_matrix(&render_camera, view);
        camera_get_projection_matrix(&render_camera, proj);

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
                   render_camera.position);
        for (uint32_t i = 0; i < outpost_level.object_count; ++i) {
            const StaticObject *object = &outpost_level.objects[i];
            DrawableGroup group = {
                .shader = lit_shader, .material = object->material, .mesh = object->mesh};
            glm_mat4_identity(group.model);
            draw_group(renderer, &group, view, proj, light_space_matrix, shadow_depth_texture,
                       render_camera.position);
        }
        for (uint32_t i = 0; i < soldier_squad.count; ++i) {
            DrawableGroup group = {
                .shader = lit_shader, .material = soldier_squad.material, .mesh = soldier_squad.mesh};
            ai_soldier_get_world_matrix(&soldier_squad, i, group.model);
            draw_group(renderer, &group, view, proj, light_space_matrix, shadow_depth_texture,
                       render_camera.position);
        }
        /* Sky must draw last - see sky.vert's file header comment on
         * the depth trick this relies on. */
        sky_draw(renderer, &sky, view, proj);
        accumulate_stats(renderer);

        framebuffer_bind_default((uint32_t)width, (uint32_t)height);
        postprocess_resolve_and_apply(postprocess, (uint32_t)width, (uint32_t)height);

#ifdef OUTPOST_HAS_EDITOR
        if (editor_active) {
            debug_overlay_draw(&debug_overlay, renderer, &player, physics_world, &g_frame_stats,
                                outpost_level.object_count, soldier_squad.count, &player_camera_motion_config);
            editor_render();
        }
#endif

        if (screenshot_path != nullptr) {
            screenshot_capture(width, height, screenshot_path);
        }

        if (smoke_test_dir != nullptr) {
            if (smoke_test_index < smoke_test_waypoint_count) {
                smoke_test_frames_at_waypoint += 1;
                if (smoke_test_frames_at_waypoint >= SMOKE_TEST_HOLD_FRAMES) {
                    char path[512];
                    snprintf(path, sizeof(path), "%s/%s.png", smoke_test_dir,
                             smoke_test_waypoints[smoke_test_index].label);
                    screenshot_capture(width, height, path);
                    LOG_INFO(OUTPOST_LOG_CATEGORY, "--smoke-test: captured %s",
                              smoke_test_waypoints[smoke_test_index].label);
                    smoke_test_index += 1;
                    smoke_test_frames_at_waypoint = 0;
                }
            } else {
                window_request_close(window);
            }
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
    game_audio_destroy(audio, &game_audio);
    infantry_entity_destroy(&player);
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
#ifdef OUTPOST_HAS_EDITOR
    if (editor_active) {
        editor_shutdown();
    }
#endif
    renderer_destroy(renderer);
    graphics_context_destroy(context);
    window_destroy(window);
    input_shutdown();
    window_shutdown();
    log_shutdown();
    return 0;
}
