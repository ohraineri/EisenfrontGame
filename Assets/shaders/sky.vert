#version 460 core

/* Unlike every other mesh in this slice, the sky dome is NOT baked to
 * world position (see primitives.h) - it must always appear to
 * surround the camera regardless of where the player stands. Instead,
 * the view matrix's translation is stripped here (mat3(uView) keeps
 * only rotation), and the clip-space z is forced to equal w so that,
 * after the perspective divide, depth is always exactly 1.0 (the far
 * plane) - the same technique the engine's own Skybox module uses
 * (see skybox.h), reimplemented here because Skybox's only entry point
 * requires a baked cubemap file, which this slice's "generate at load
 * time, no files" decision rules out.
 */
layout(location = 0) in vec3 aPosition;

uniform mat4 uView;
uniform mat4 uProj;

out vec3 vDirection;

void main() {
    vDirection = aPosition;
    mat4 rotationOnlyView = mat4(mat3(uView));
    vec4 clipPosition = uProj * rotationOnlyView * vec4(aPosition, 1.0);
    gl_Position = clipPosition.xyww;
}
