#include "ground_texture.h"

#include <math.h>
#include <stdlib.h>

static uint32_t hash2(int32_t x, int32_t y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float value_at(int32_t x, int32_t y, uint32_t period, uint32_t seed) {
    /* Wrap the lattice so the noise (and therefore the texture) tiles
     * seamlessly at `period` - the ground plane repeats this texture
     * many times across the outpost. */
    const int32_t wrapped_x = ((x % (int32_t)period) + (int32_t)period) % (int32_t)period;
    const int32_t wrapped_y = ((y % (int32_t)period) + (int32_t)period) % (int32_t)period;
    return (float)(hash2(wrapped_x, wrapped_y, seed) & 0xFFFFu) / 65535.0f;
}

static float smoothstep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

static float value_noise(float x, float y, uint32_t period, uint32_t seed) {
    const int32_t x0 = (int32_t)floorf(x);
    const int32_t y0 = (int32_t)floorf(y);
    const float   fx = smoothstep(x - (float)x0);
    const float   fy = smoothstep(y - (float)y0);

    const float v00 = value_at(x0, y0, period, seed);
    const float v10 = value_at(x0 + 1, y0, period, seed);
    const float v01 = value_at(x0, y0 + 1, period, seed);
    const float v11 = value_at(x0 + 1, y0 + 1, period, seed);

    const float top = v00 + (v10 - v00) * fx;
    const float bottom = v01 + (v11 - v01) * fx;
    return top + (bottom - top) * fy;
}

static float fractal_noise(float x, float y, uint32_t period, uint32_t seed) {
    float value = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float total_amplitude = 0.0f;
    for (int octave = 0; octave < 5; ++octave) {
        const uint32_t octave_period = period * (uint32_t)(1 << octave);
        value += value_noise(x * frequency, y * frequency, octave_period, seed + (uint32_t)octave * 101u) *
                 amplitude;
        total_amplitude += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return value / total_amplitude;
}

Result ground_texture_create(uint32_t size, uint32_t seed, Texture **out_texture) {
    if (size == 0 || out_texture == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    uint8_t *pixels = malloc((size_t)size * size * 4u);
    if (pixels == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    /* Dry dirt/gravel palette: a dark-to-light brown gradient driven by
     * fractal value noise, plus a sparser higher-frequency layer for
     * small gravel speckle. */
    const float base_dark[3] = {0.16f, 0.13f, 0.10f};
    const float base_light[3] = {0.42f, 0.35f, 0.26f};

    for (uint32_t y = 0; y < size; ++y) {
        for (uint32_t x = 0; x < size; ++x) {
            const float u = (float)x / (float)size * 8.0f;
            const float v = (float)y / (float)size * 8.0f;
            const float macro = fractal_noise(u, v, 8u, seed);
            const float speckle = value_noise(u * 6.0f, v * 6.0f, 48u, seed + 977u);
            float       t = macro * 0.8f + speckle * 0.2f;
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

            const size_t index = ((size_t)y * size + x) * 4u;
            for (int c = 0; c < 3; ++c) {
                const float channel = base_dark[c] + (base_light[c] - base_dark[c]) * t;
                pixels[index + (size_t)c] = (uint8_t)(channel * 255.0f);
            }
            pixels[index + 3] = 255u;
        }
    }

    TextureParams params = texture_params_default();
    params.wrap_s = TEXTURE_WRAP_REPEAT;
    params.wrap_t = TEXTURE_WRAP_REPEAT;
    params.min_filter = TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR;
    params.mag_filter = TEXTURE_FILTER_LINEAR;
    params.generate_mipmaps = true;

    const Result result =
        texture_create_from_pixels(size, size, TEXTURE_FORMAT_RGBA8, pixels, &params, out_texture);
    free(pixels);
    return result;
}
