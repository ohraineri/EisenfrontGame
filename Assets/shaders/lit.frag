#version 460 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vLightSpacePos;

out vec4 FragColor;

/* Matches MaterialParamsGpu in engine/src/material/material.c exactly
 * (std140 layout) - Material's own params UBO, bound by material_bind()
 * to MATERIAL_UBO_BINDING = 1. */
layout(std140, binding = 1) uniform MaterialParams {
    vec4  uBaseColor;
    vec4  uEmissiveFactor;
    float uMetallicFactor;
    float uRoughnessFactor;
    float uAoStrength;
    float _materialPad0;
};

/* material_bind() only binds texture units via glActiveTexture/
 * glBindTexture - it never sets a sampler uniform value - so these
 * bindings must be explicit (GLSL 4.2+), matching
 * MATERIAL_TEXTURE_UNIT_DIFFUSE (0) in material.h. */
layout(binding = 0) uniform sampler2D uDiffuseMap;
/* Not one of Material's own six slots (0-5, see material.h) - the
 * shadow map is bound directly by Game code, not through Material. */
layout(binding = 6) uniform sampler2D uShadowMap;

struct PointLightGpu {
    vec4 position; /* xyz, radius in w */
    vec4 color;    /* rgb, intensity in w */
};
struct SpotLightGpu {
    vec4 position;  /* xyz, radius in w */
    vec4 direction; /* xyz, inner_cone_cos in w */
    vec4 color;     /* rgb, intensity in w */
    vec4 cone;      /* outer_cone_cos, then unused */
};

/* Matches LightingGpuData in engine/src/renderer_fx/lighting.c exactly,
 * bound by lighting_bind() to LIGHTING_UBO_BINDING = 2. */
layout(std140, binding = 2) uniform Lighting {
    vec4 uDirDirection; /* xyz, unused w - points FROM light TOWARD scene */
    vec4 uDirColor;     /* rgb, intensity in w */
    int  uHasDirectional;
    int  uPointLightCount;
    int  uSpotLightCount;
    int  _lightingPad0;
    PointLightGpu uPointLights[8];
    SpotLightGpu  uSpotLights[4];
};

uniform vec3  uCameraPos;
uniform vec3  uFogColor;
uniform float uFogDensity;

/* 1.0 = fully in shadow, 0.0 = fully lit. A single hard tap with a
 * fixed depth bias - no PCF/softening (shadow.h itself documents no
 * cascades either; this matches that same "prove the pipeline, not a
 * shadow-quality showcase" scope). */
float shadow_amount(vec4 lightSpacePos) {
    vec3 projected = lightSpacePos.xyz / lightSpacePos.w;
    projected = projected * 0.5 + 0.5;
    if (projected.z > 1.0 || projected.x < 0.0 || projected.x > 1.0 || projected.y < 0.0 ||
        projected.y > 1.0) {
        return 0.0;
    }
    float closestDepth = texture(uShadowMap, projected.xy).r;
    float currentDepth = projected.z;
    float bias = 0.003;
    return (currentDepth - bias > closestDepth) ? 1.0 : 0.0;
}

void main() {
    vec3 normal = normalize(vNormal);
    vec3 albedo = uBaseColor.rgb * texture(uDiffuseMap, vUV).rgb;

    /* Simple ambient + Lambertian directional sun, no full PBR - a
     * vertical slice proving the pipeline, not a lighting model demo.
     * uMetallicFactor/uRoughnessFactor/uAoStrength are read here only
     * to keep the UBO layout honest with material.h's documented
     * fields; a later pass can put them to real use. Ambient is high
     * relative to a clear-sky scene on purpose: this outpost's chosen
     * atmosphere is a hazy overcast morning, where scattered skylight
     * dominates and direct-sun shadow contrast is naturally low - a
     * low ambient term here would read as an unrealistic clear-sky
     * hard-shadow look this scene isn't going for. */
    vec3 ambient = albedo * 0.4;
    vec3 lit = vec3(0.0);
    if (uHasDirectional != 0) {
        vec3        lightDir = normalize(-uDirDirection.xyz);
        float       ndotl = max(dot(normal, lightDir), 0.0);
        vec3        radiance = uDirColor.rgb * uDirColor.a;
        float       shadow = shadow_amount(vLightSpacePos);
        lit += albedo * radiance * ndotl * (1.0 - shadow) * (1.0 - uAoStrength * 0.0);
    }
    for (int i = 0; i < uPointLightCount; ++i) {
        vec3  toLight = uPointLights[i].position.xyz - vWorldPos;
        float dist = length(toLight);
        float radius = max(uPointLights[i].position.w, 0.001);
        float atten = clamp(1.0 - (dist / radius), 0.0, 1.0);
        atten *= atten;
        vec3  lightDir = toLight / max(dist, 0.0001);
        float ndotl = max(dot(normal, lightDir), 0.0);
        lit += albedo * uPointLights[i].color.rgb * uPointLights[i].color.a * ndotl * atten;
    }

    vec3 color = ambient + lit + uEmissiveFactor.rgb;

    float dist = length(uCameraPos - vWorldPos);
    float fog = 1.0 - exp(-uFogDensity * dist);
    color = mix(color, uFogColor, clamp(fog, 0.0, 1.0));

    FragColor = vec4(color, uBaseColor.a);
}
