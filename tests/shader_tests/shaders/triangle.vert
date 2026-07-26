#version 460 core

layout(location = 0) in vec2 aPos;

uniform mat4 uModel;
uniform vec3 uTint;

out vec3 vTint;

void main() {
    vTint = uTint;
    gl_Position = uModel * vec4(aPos, 0.0, 1.0);
}
