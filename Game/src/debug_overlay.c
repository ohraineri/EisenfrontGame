#include "debug_overlay.h"

#include "eisenfront/editor.h"

void debug_overlay_toggle(DebugOverlay *overlay) {
    overlay->visible = !overlay->visible;
}

void debug_overlay_draw(DebugOverlay *overlay, Renderer *renderer, InfantryEntity *player,
                         PhysicsWorld *physics_world, const RendererStats *frame_stats,
                         uint32_t static_object_count, uint32_t soldier_count,
                         CameraMotionConfig *camera_motion_config) {
    if (!overlay->visible) {
        return;
    }

    InfantryViewComponent       *view = infantry_entity_get_view(player);
    InfantryLocomotionComponent *locomotion = infantry_entity_get_locomotion(player);
    InfantryTransformComponent  *transform = infantry_entity_get_transform(player);

    igBegin("Outpost Debug (F1)", &overlay->visible, 0);

    igText("Draw calls: %u", frame_stats->draw_calls);
    igText("Triangles: %u", frame_stats->triangles);
    igText("State changes: %u", frame_stats->state_changes);
    igSeparator();
    igText("Static objects: %u", static_object_count);
    igText("AI soldiers: %u", soldier_count);
    igText("Player position: %.1f, %.1f, %.1f", view->camera.position[0], view->camera.position[1],
           view->camera.position[2]);
    igText("Stance: %s", locomotion->stance == PLAYER_STANCE_CROUCHING ? "Crouching" : "Standing");
    {
        const char *vertical_state_name = "Grounded";
        switch (locomotion->vertical_state) {
            case PLAYER_VERTICAL_STATE_JUMP_ASCENDING:
                vertical_state_name = "JumpAscending";
                break;
            case PLAYER_VERTICAL_STATE_FALLING:
                vertical_state_name = "Falling";
                break;
            case PLAYER_VERTICAL_STATE_LANDING_RECOVERY:
                vertical_state_name = "LandingRecovery";
                break;
            case PLAYER_VERTICAL_STATE_GROUNDED:
            default:
                break;
        }
        igText("Vertical state: %s", vertical_state_name);
    }
    if (locomotion->has_landing_event) {
        igText("Last landing: impact %.1f m/s, air %.2fs, severity %d",
               locomotion->last_landing_event.impact_speed, locomotion->last_landing_event.air_time_seconds,
               (int)locomotion->last_landing_event.severity);
    }
    igSeparator();

    if (igCheckbox("Wireframe", &overlay->wireframe)) {
        renderer_set_wireframe(renderer, overlay->wireframe);
    }
    igCheckbox("Noclip", &view->noclip_enabled);
    igSeparator();

    igText("Camera motion (0..1)");
    igSliderFloat("Global", &camera_motion_config->global_intensity, 0.0f, 1.0f, "%.2f", 0);
    igSliderFloat("Gait", &camera_motion_config->gait_intensity, 0.0f, 1.0f, "%.2f", 0);
    igSliderFloat("Landing", &camera_motion_config->landing_intensity, 0.0f, 1.0f, "%.2f", 0);
    igSliderFloat("Inertia", &camera_motion_config->inertia_intensity, 0.0f, 1.0f, "%.2f", 0);
    igSeparator();

    if (igButton("Teleport to look direction", (ImVec2){0.0f, 0.0f})) {
        vec3 forward;
        camera_get_forward(&view->camera, forward);
        RaycastHit hit;
        if (physics_raycast(physics_world, view->camera.position, forward, 200.0f, COLLISION_LAYER_ALL,
                             &hit)) {
            vec3 destination = {hit.point[0], hit.point[1] + 1.0f, hit.point[2]};
            character_controller_set_position(locomotion->controller, destination);
            glm_vec3_copy(destination, transform->position);
            view->camera.position[0] = destination[0];
            view->camera.position[1] = destination[1] + locomotion->eye_height_offset;
            view->camera.position[2] = destination[2];
            locomotion->vertical_velocity = 0.0f;
            /* Teleport, exactly like the smoke-test waypoint branch -
             * never let a stale partial stride surface as a phantom
             * footstep afterward. */
            infantry_entity_reset_gait(player);
        }
    }

    igEnd();
}
