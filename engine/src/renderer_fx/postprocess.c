#include "eisenfront/postprocess.h"

#include "renderer_fx_internal.h"

#include <stdlib.h>

static const char *const k_fullscreen_vertex_source =
    "#version 460 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec2 aUV;\n"
    "out vec2 vUV;\n"
    "void main() { vUV = aUV; gl_Position = vec4(aPos, 0.0, 1.0); }\n";

static const char *const k_bright_pass_fragment_source =
    "#version 460 core\n"
    "in vec2 vUV;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D uScene;\n"
    "uniform float uThreshold;\n"
    "void main() {\n"
    "    vec3 color = texture(uScene, vUV).rgb;\n"
    "    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));\n"
    "    FragColor = luminance > uThreshold ? vec4(color, 1.0) : vec4(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

static const char *const k_blur_fragment_source =
    "#version 460 core\n"
    "in vec2 vUV;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D uImage;\n"
    "uniform vec2 uTexelStep;\n"
    "void main() {\n"
    "    float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);\n"
    "    vec3 result = texture(uImage, vUV).rgb * weights[0];\n"
    "    for (int i = 1; i < 5; ++i) {\n"
    "        result += texture(uImage, vUV + uTexelStep * float(i)).rgb * weights[i];\n"
    "        result += texture(uImage, vUV - uTexelStep * float(i)).rgb * weights[i];\n"
    "    }\n"
    "    FragColor = vec4(result, 1.0);\n"
    "}\n";

static const char *const k_composite_fragment_source =
    "#version 460 core\n"
    "in vec2 vUV;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D uScene;\n"
    "uniform sampler2D uBloom;\n"
    "uniform bool uUseBloom;\n"
    "uniform float uExposure;\n"
    "void main() {\n"
    "    vec3 color = texture(uScene, vUV).rgb;\n"
    "    if (uUseBloom) { color += texture(uBloom, vUV).rgb; }\n"
    "    color *= uExposure;\n"
    "    color = color / (color + vec3(1.0));\n"       /* Reinhard tone mapping */
    "    color = pow(color, vec3(1.0 / 2.2));\n"        /* gamma correction */
    "    FragColor = vec4(color, 1.0);\n"
    "}\n";

static const float k_fullscreen_quad[24] = {
    -1, -1, 0, 0, 1, -1, 1, 0, 1, 1, 1, 1, -1, -1, 0, 0, 1, 1, 1, 1, -1, 1, 0, 1,
};

struct PostProcess {
    Framebuffer *scene_fb;
    Framebuffer *resolve_fb;
    Framebuffer *bloom_a;
    Framebuffer *bloom_b;
    bool         bloom_enabled;
    float        exposure;
    float        bloom_threshold;
    uint32_t     width;
    uint32_t     height;

    GLuint       bright_pass_program;
    GLuint       blur_program;
    GLuint       composite_program;
    GLint        bright_scene_location;
    GLint        bright_threshold_location;
    GLint        blur_image_location;
    GLint        blur_texel_step_location;
    GLint        composite_scene_location;
    GLint        composite_bloom_location;
    GLint        composite_use_bloom_location;
    GLint        composite_exposure_location;

    GpuBuffer   *quad_vertex_buffer;
    VertexArray *quad_vertex_array;
};

PostProcessDesc postprocess_desc_default(uint32_t width, uint32_t height) {
    return (PostProcessDesc){
        .width = width,
        .height = height,
        .msaa_samples = 1,
        .enable_bloom = true,
        .exposure = 1.0f,
        .bloom_threshold = 1.0f,
    };
}

static Result create_framebuffer(uint32_t width, uint32_t height, uint32_t samples, bool depth,
                                  Framebuffer **out_fb) {
    const FramebufferDesc desc = {
        .width = width,
        .height = height,
        .sample_count = samples,
        .has_color = true,
        .color_format = FRAMEBUFFER_COLOR_RGBA16F,
        .has_depth = depth,
    };
    return framebuffer_create(&desc, out_fb);
}

Result postprocess_create(const PostProcessDesc *desc, PostProcess **out_postprocess) {
    if (desc == nullptr || out_postprocess == nullptr || desc->width == 0 || desc->height == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    PostProcess *pp = calloc(1, sizeof(*pp));
    if (pp == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    pp->width = desc->width;
    pp->height = desc->height;
    pp->bloom_enabled = desc->enable_bloom;
    pp->exposure = desc->exposure;
    pp->bloom_threshold = desc->bloom_threshold;

    Result result = create_framebuffer(desc->width, desc->height, desc->msaa_samples, true, &pp->scene_fb);
    if (result != RESULT_OK) {
        free(pp);
        return result;
    }
    result = create_framebuffer(desc->width, desc->height, 1, false, &pp->resolve_fb);
    if (result != RESULT_OK) {
        framebuffer_destroy(pp->scene_fb);
        free(pp);
        return result;
    }

    if (pp->bloom_enabled) {
        const uint32_t bloom_width = desc->width / 2 > 0 ? desc->width / 2 : 1;
        const uint32_t bloom_height = desc->height / 2 > 0 ? desc->height / 2 : 1;
        result = create_framebuffer(bloom_width, bloom_height, 1, false, &pp->bloom_a);
        if (result != RESULT_OK) {
            framebuffer_destroy(pp->resolve_fb);
            framebuffer_destroy(pp->scene_fb);
            free(pp);
            return result;
        }
        result = create_framebuffer(bloom_width, bloom_height, 1, false, &pp->bloom_b);
        if (result != RESULT_OK) {
            framebuffer_destroy(pp->bloom_a);
            framebuffer_destroy(pp->resolve_fb);
            framebuffer_destroy(pp->scene_fb);
            free(pp);
            return result;
        }
    }

    pp->bright_pass_program =
        renderer_fx_link_program(k_fullscreen_vertex_source, k_bright_pass_fragment_source);
    pp->blur_program = renderer_fx_link_program(k_fullscreen_vertex_source, k_blur_fragment_source);
    pp->composite_program =
        renderer_fx_link_program(k_fullscreen_vertex_source, k_composite_fragment_source);
    if (pp->bright_pass_program == 0 || pp->blur_program == 0 || pp->composite_program == 0) {
        postprocess_destroy(pp);
        return RESULT_ERROR_PLATFORM;
    }
    pp->bright_scene_location = glGetUniformLocation(pp->bright_pass_program, "uScene");
    pp->bright_threshold_location = glGetUniformLocation(pp->bright_pass_program, "uThreshold");
    pp->blur_image_location = glGetUniformLocation(pp->blur_program, "uImage");
    pp->blur_texel_step_location = glGetUniformLocation(pp->blur_program, "uTexelStep");
    pp->composite_scene_location = glGetUniformLocation(pp->composite_program, "uScene");
    pp->composite_bloom_location = glGetUniformLocation(pp->composite_program, "uBloom");
    pp->composite_use_bloom_location = glGetUniformLocation(pp->composite_program, "uUseBloom");
    pp->composite_exposure_location = glGetUniformLocation(pp->composite_program, "uExposure");

    const BufferDesc quad_desc = {
        .type = BUFFER_TYPE_VERTEX,
        .usage = BUFFER_USAGE_STATIC,
        .initial_data = k_fullscreen_quad,
        .size_bytes = sizeof(k_fullscreen_quad),
    };
    result = gpu_buffer_create(&quad_desc, &pp->quad_vertex_buffer);
    if (result != RESULT_OK) {
        postprocess_destroy(pp);
        return result;
    }
    const VertexAttribute attributes[2] = {
        {.location = 0,
         .component_count = 2,
         .type = VERTEX_ATTRIB_TYPE_FLOAT,
         .stride_bytes = sizeof(float) * 4,
         .offset_bytes = 0},
        {.location = 1,
         .component_count = 2,
         .type = VERTEX_ATTRIB_TYPE_FLOAT,
         .stride_bytes = sizeof(float) * 4,
         .offset_bytes = sizeof(float) * 2},
    };
    const VertexArrayDesc array_desc = {
        .vertex_buffer = pp->quad_vertex_buffer, .attributes = attributes, .attribute_count = 2};
    result = vertex_array_create(&array_desc, &pp->quad_vertex_array);
    if (result != RESULT_OK) {
        postprocess_destroy(pp);
        return result;
    }

    *out_postprocess = pp;
    return RESULT_OK;
}

void postprocess_destroy(PostProcess *pp) {
    if (pp == nullptr) {
        return;
    }
    vertex_array_destroy(pp->quad_vertex_array);
    gpu_buffer_destroy(pp->quad_vertex_buffer);
    if (pp->composite_program != 0) {
        glDeleteProgram(pp->composite_program);
    }
    if (pp->blur_program != 0) {
        glDeleteProgram(pp->blur_program);
    }
    if (pp->bright_pass_program != 0) {
        glDeleteProgram(pp->bright_pass_program);
    }
    framebuffer_destroy(pp->bloom_b);
    framebuffer_destroy(pp->bloom_a);
    framebuffer_destroy(pp->resolve_fb);
    framebuffer_destroy(pp->scene_fb);
    free(pp);
}

void postprocess_begin_scene(PostProcess *pp) {
    ASSERT(pp != nullptr);
    if (pp == nullptr) {
        return;
    }
    framebuffer_bind(pp->scene_fb);
}

static void draw_fullscreen_quad(const PostProcess *pp) {
    const bool depth_was_enabled = glIsEnabled(GL_DEPTH_TEST) != GL_FALSE;
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(vertex_array_get_gl_handle(pp->quad_vertex_array));
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    if (depth_was_enabled) {
        glEnable(GL_DEPTH_TEST);
    }
}

void postprocess_resolve_and_apply(PostProcess *pp, uint32_t output_width, uint32_t output_height) {
    ASSERT(pp != nullptr);
    if (pp == nullptr) {
        return;
    }

    /* The bloom ping-pong loop below (when enabled) leaves whichever of
     * bloom_a/bloom_b it last wrote to as the bound framebuffer - a
     * half-resolution internal target, never the caller's intended
     * output. Capturing it here and restoring it right before the
     * composite draw is what makes good on this function's own
     * documented contract ("writing to whatever framebuffer is
     * currently bound to GL_DRAW_FRAMEBUFFER" - i.e. bound at the
     * moment the caller invoked this function, not wherever an
     * internal pass happened to leave it). Both the draw AND read
     * targets need capturing/restoring: framebuffer_bind() (used
     * throughout the bloom loop below) binds via plain GL_FRAMEBUFFER,
     * which sets both simultaneously, so leaving GL_READ_FRAMEBUFFER on
     * a stale bloom buffer would make a caller's subsequent
     * glReadPixels() (e.g. a screenshot) silently read the wrong,
     * wrong-sized buffer even though the composite draw itself lands
     * correctly on GL_DRAW_FRAMEBUFFER. */
    GLint caller_framebuffer = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &caller_framebuffer);

    framebuffer_resolve_to(pp->scene_fb, pp->resolve_fb);

    uint32_t bloom_result_texture = 0;
    if (pp->bloom_enabled) {
        framebuffer_bind(pp->bloom_a);
        glUseProgram(pp->bright_pass_program);
        glUniform1f(pp->bright_threshold_location, pp->bloom_threshold);
        glBindTextureUnit(0, framebuffer_get_color_texture(pp->resolve_fb));
        glUniform1i(pp->bright_scene_location, 0);
        draw_fullscreen_quad(pp);

        Framebuffer   *src = pp->bloom_a;
        Framebuffer   *dst = pp->bloom_b;
        const uint32_t bloom_width = framebuffer_get_width(pp->bloom_a);
        const uint32_t bloom_height = framebuffer_get_height(pp->bloom_a);
        glUseProgram(pp->blur_program);
        glUniform1i(pp->blur_image_location, 0);

        /* Four ping-pong passes (two horizontal, two vertical, alternating):
         * a separable blur applies the horizontal and vertical 1D
         * kernels independently, which is mathematically equivalent to
         * one 2D Gaussian pass but far cheaper (2*5 samples instead of
         * 25); repeating the pair widens the effective blur radius. */
        for (int pass = 0; pass < 4; ++pass) {
            framebuffer_bind(dst);
            const bool  horizontal = (pass % 2) == 0;
            const float step_x = horizontal ? 1.0f / (float)bloom_width : 0.0f;
            const float step_y = horizontal ? 0.0f : 1.0f / (float)bloom_height;
            glUniform2f(pp->blur_texel_step_location, step_x, step_y);
            glBindTextureUnit(0, framebuffer_get_color_texture(src));
            draw_fullscreen_quad(pp);

            Framebuffer *temp = src;
            src = dst;
            dst = temp;
        }
        bloom_result_texture = framebuffer_get_color_texture(src);
    }

    /* GL_FRAMEBUFFER, not GL_DRAW_FRAMEBUFFER: restores both the draw
     * and read targets in one call, matching how framebuffer_bind()
     * itself always binds both together (see the comment above). */
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)caller_framebuffer);
    glViewport(0, 0, (GLsizei)output_width, (GLsizei)output_height);
    glUseProgram(pp->composite_program);
    glBindTextureUnit(0, framebuffer_get_color_texture(pp->resolve_fb));
    glUniform1i(pp->composite_scene_location, 0);
    if (pp->bloom_enabled) {
        glBindTextureUnit(1, bloom_result_texture);
        glUniform1i(pp->composite_bloom_location, 1);
    }
    glUniform1i(pp->composite_use_bloom_location, pp->bloom_enabled ? 1 : 0);
    glUniform1f(pp->composite_exposure_location, pp->exposure);
    draw_fullscreen_quad(pp);
}

void postprocess_set_exposure(PostProcess *pp, float exposure) {
    ASSERT(pp != nullptr);
    if (pp != nullptr) {
        pp->exposure = exposure;
    }
}
