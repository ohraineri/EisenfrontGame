#version 460 core

in vec3 vTint;
out vec4 FragColor;

uniform float uAlpha;

void main() {
    FragColor = vec4(vTint, uAlpha);
}
