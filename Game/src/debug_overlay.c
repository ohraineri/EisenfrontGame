#include "debug_overlay.h"

#include "eisenfront/editor.h"

void debug_overlay_toggle(DebugOverlay *overlay) {
    overlay->visible = !overlay->visible;
}

void debug_overlay_draw(DebugOverlay *overlay, Renderer *renderer, Player *player,
                         PhysicsWorld *physics_world, const RendererStats *frame_stats,
                         uint32_t static_object_count, uint32_t soldier_count) {
    if (!overlay->visible) {
        return;
    }

    igBegin("Outpost Debug (F1)", &overlay->visible, 0);

    igText("Draw calls: %u", frame_stats->draw_calls);
    igText("Triangles: %u", frame_stats->triangles);
    igText("State changes: %u", frame_stats->state_changes);
    igSeparator();
    igText("Static objects: %u", static_object_count);
    igText("AI soldiers: %u", soldier_count);
    igText("Player position: %.1f, %.1f, %.1f", player->camera.position[0],
           player->camera.position[1], player->camera.position[2]);
    igSeparator();

    if (igCheckbox("Wireframe", &overlay->wireframe)) {
        renderer_set_wireframe(renderer, overlay->wireframe);
    }
    igCheckbox("Noclip", &player->noclip_enabled);

    if (igButton("Teleport to look direction", (ImVec2){0.0f, 0.0f})) {
        vec3 forward;
        camera_get_forward(&player->camera, forward);
        RaycastHit hit;
        if (physics_raycast(physics_world, player->camera.position, forward, 200.0f,
                             COLLISION_LAYER_ALL, &hit)) {
            vec3 destination = {hit.point[0], hit.point[1] + 1.0f, hit.point[2]};
            character_controller_set_position(player->controller, destination);
            player->camera.position[0] = destination[0];
            player->camera.position[1] = destination[1] + player->eye_height_offset;
            player->camera.position[2] = destination[2];
            player->vertical_velocity = 0.0f;
        }
    }

    igEnd();
}
