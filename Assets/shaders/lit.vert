#version 460 core

/* Static geometry only: vertex positions/normals are already baked into
 * world space at mesh-build time (see Game/src/outpost_scene.c) rather
 * than transformed here by a per-object model matrix uniform. Renderer
 * defers every submit()ted draw to a single sorted pass inside
 * renderer_end_frame(), and glUniform values are persistent GL program
 * state - not per-draw - so a "uModel" uniform set between two
 * renderer_submit() calls sharing this shader would be overwritten
 * before either draw actually executes. Baking world-space geometry
 * once at load time sidesteps that entirely for anything that never
 * moves; a handful of dynamic entities (Phase 6 AI soldiers) instead
 * re-bake their own small dynamic Mesh's vertices every time they move.
 */
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aTangent;
layout(location = 3) in vec2 aUV;

uniform mat4 uView;
uniform mat4 uProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vWorldPos = aPosition;
    vNormal = aNormal;
    vUV = aUV;
    gl_Position = uProj * uView * vec4(aPosition, 1.0);
}
