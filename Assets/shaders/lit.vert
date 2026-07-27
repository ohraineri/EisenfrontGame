#version 460 core

/* Most geometry in this slice is static and pre-baked into world space
 * at mesh-build time (see Game/src/outpost_scene.c) rather than
 * transformed here by uModel - see the file header comment in main.c
 * on why per-object varying uniforms are only safe when each object
 * gets its own immediate flush (begin_frame+submit+end_frame), which
 * is how every draw in this slice works, static or not. Static
 * objects simply always pass uModel = identity; dynamic ones (Phase 6
 * AI soldiers - built in object space, centered on their own origin)
 * pass their real per-frame transform. uModel has no scale, only
 * rotation and translation, so mat3(uModel) is already the correct
 * normal transform - no inverse-transpose needed.
 */
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aTangent;
layout(location = 3) in vec2 aUV;

uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uModel;
uniform mat4 uLightSpaceMatrix;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vLightSpacePos;

void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = mat3(uModel) * aNormal;
    vUV = aUV;
    vLightSpacePos = uLightSpaceMatrix * worldPos;
    gl_Position = uProj * uView * worldPos;
}
