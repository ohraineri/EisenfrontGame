#version 460 core

in vec3 vDirection;
out vec4 FragColor;

uniform vec3 uHorizonColor;
uniform vec3 uZenithColor;
uniform vec3 uSunDirection; /* points FROM the sun TOWARD the scene, same convention as DirectionalLight */
uniform vec3 uSunColor;

void main() {
    vec3  dir = normalize(vDirection);
    float elevation = clamp(dir.y, 0.0, 1.0);
    /* sqrt rather than a linear lerp: keeps more of the gradient near
     * the horizon, where a real hazy sky actually changes color the
     * fastest, instead of spreading it evenly up to the zenith. */
    vec3 sky = mix(uHorizonColor, uZenithColor, sqrt(elevation));

    vec3  towardSun = normalize(-uSunDirection);
    float sunDot = max(dot(dir, towardSun), 0.0);
    float sunDisc = pow(sunDot, 2000.0);
    float sunGlow = pow(sunDot, 8.0) * 0.35;

    vec3 color = sky + uSunColor * (sunDisc * 4.0 + sunGlow);
    FragColor = vec4(color, 1.0);
}
