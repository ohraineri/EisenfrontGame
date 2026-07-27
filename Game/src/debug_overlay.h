/*
 * F1-toggled Editor debug overlay: real per-frame RendererStats,
 * static/dynamic object counts, a wireframe toggle, noclip, and
 * teleport-to-cursor. Entirely absent from Release builds - see
 * editor.h's own file header comment on why Editor doesn't exist
 * outside EISENFRONT_BUILD_EDITOR - so every call site in main.c using
 * this header must itself be guarded by #ifdef OUTPOST_HAS_EDITOR.
 */
#ifndef OUTPOST_DEBUG_OVERLAY_H
#define OUTPOST_DEBUG_OVERLAY_H

#include "eisenfront/physics.h"
#include "eisenfront/renderer.h"

#include "camera_motion.h"
#include "infantry_entity.h"

typedef struct DebugOverlay {
    bool visible;
    bool wireframe;
} DebugOverlay;

/* Toggle visibility - call on the F1 key-press edge. */
void debug_overlay_toggle(DebugOverlay *overlay);

/* Draws the overlay (if visible) and applies whatever the user did
 * this frame (wireframe checkbox, noclip checkbox, teleport button -
 * the last one raycasts along the camera's forward direction and
 * teleports the player's CharacterController to the hit point). Call
 * between editor_new_frame() and editor_render(); always safe to call
 * even while hidden (does nothing but stay ready for the next F1). */
void debug_overlay_draw(DebugOverlay *overlay, Renderer *renderer, InfantryEntity *player,
                         PhysicsWorld *physics_world, const RendererStats *frame_stats,
                         uint32_t static_object_count, uint32_t soldier_count,
                         CameraMotionConfig *camera_motion_config);

#endif /* OUTPOST_DEBUG_OVERLAY_H */
