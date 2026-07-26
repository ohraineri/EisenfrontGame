#include "eisenfront/framebuffer.h"

#include <stdlib.h>

#define FRAMEBUFFER_LOG_CATEGORY "framebuffer"

struct Framebuffer {
    uint32_t gl_fbo;
    uint32_t color_texture;
    uint32_t depth_texture;
    uint32_t width;
    uint32_t height;
    uint32_t sample_count;
};

static void apply_sample_texture_params(GLuint texture, GLenum min_filter, GLenum mag_filter) {
    glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, (GLint)min_filter);
    glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, (GLint)mag_filter);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(texture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

Result framebuffer_create(const FramebufferDesc *desc, Framebuffer **out_framebuffer) {
    if (desc == nullptr || out_framebuffer == nullptr || desc->width == 0 || desc->height == 0) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (!desc->has_color && !desc->has_depth) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const bool   multisampled = desc->sample_count > 1;
    const GLenum target = multisampled ? GL_TEXTURE_2D_MULTISAMPLE : GL_TEXTURE_2D;

    GLuint gl_fbo = 0;
    glCreateFramebuffers(1, &gl_fbo);

    GLuint color_texture = 0;
    if (desc->has_color) {
        glCreateTextures(target, 1, &color_texture);
        const GLenum internal_format =
            (desc->color_format == FRAMEBUFFER_COLOR_RGBA16F) ? GL_RGBA16F : GL_RGBA8;
        if (multisampled) {
            glTextureStorage2DMultisample(color_texture, (GLsizei)desc->sample_count,
                                           internal_format, (GLsizei)desc->width,
                                           (GLsizei)desc->height, GL_TRUE);
        } else {
            glTextureStorage2D(color_texture, 1, internal_format, (GLsizei)desc->width,
                                (GLsizei)desc->height);
            apply_sample_texture_params(color_texture, GL_LINEAR, GL_LINEAR);
        }
        glNamedFramebufferTexture(gl_fbo, GL_COLOR_ATTACHMENT0, color_texture, 0);
    } else {
        glNamedFramebufferDrawBuffer(gl_fbo, GL_NONE);
        glNamedFramebufferReadBuffer(gl_fbo, GL_NONE);
    }

    GLuint depth_texture = 0;
    if (desc->has_depth) {
        glCreateTextures(target, 1, &depth_texture);
        if (multisampled) {
            glTextureStorage2DMultisample(depth_texture, (GLsizei)desc->sample_count,
                                           GL_DEPTH_COMPONENT24, (GLsizei)desc->width,
                                           (GLsizei)desc->height, GL_TRUE);
        } else {
            glTextureStorage2D(depth_texture, 1, GL_DEPTH_COMPONENT24, (GLsizei)desc->width,
                                (GLsizei)desc->height);
            apply_sample_texture_params(depth_texture, GL_NEAREST, GL_NEAREST);
        }
        glNamedFramebufferTexture(gl_fbo, GL_DEPTH_ATTACHMENT, depth_texture, 0);
    }

    const GLenum status = glCheckNamedFramebufferStatus(gl_fbo, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG_ERROR(FRAMEBUFFER_LOG_CATEGORY, "Framebuffer incomplete: 0x%x", status);
        glDeleteFramebuffers(1, &gl_fbo);
        if (color_texture != 0) {
            glDeleteTextures(1, &color_texture);
        }
        if (depth_texture != 0) {
            glDeleteTextures(1, &depth_texture);
        }
        return RESULT_ERROR_PLATFORM;
    }

    Framebuffer *framebuffer = malloc(sizeof(*framebuffer));
    if (framebuffer == nullptr) {
        glDeleteFramebuffers(1, &gl_fbo);
        if (color_texture != 0) {
            glDeleteTextures(1, &color_texture);
        }
        if (depth_texture != 0) {
            glDeleteTextures(1, &depth_texture);
        }
        return RESULT_ERROR_OUT_OF_MEMORY;
    }
    framebuffer->gl_fbo = gl_fbo;
    framebuffer->color_texture = color_texture;
    framebuffer->depth_texture = depth_texture;
    framebuffer->width = desc->width;
    framebuffer->height = desc->height;
    framebuffer->sample_count = multisampled ? desc->sample_count : 1;

    *out_framebuffer = framebuffer;
    return RESULT_OK;
}

void framebuffer_destroy(Framebuffer *framebuffer) {
    if (framebuffer == nullptr) {
        return;
    }
    if (framebuffer->color_texture != 0) {
        glDeleteTextures(1, &framebuffer->color_texture);
    }
    if (framebuffer->depth_texture != 0) {
        glDeleteTextures(1, &framebuffer->depth_texture);
    }
    glDeleteFramebuffers(1, &framebuffer->gl_fbo);
    free(framebuffer);
}

void framebuffer_bind(const Framebuffer *framebuffer) {
    ASSERT(framebuffer != nullptr);
    if (framebuffer == nullptr) {
        return;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer->gl_fbo);
    glViewport(0, 0, (GLsizei)framebuffer->width, (GLsizei)framebuffer->height);
}

void framebuffer_bind_default(uint32_t width, uint32_t height) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
}

uint32_t framebuffer_get_color_texture(const Framebuffer *framebuffer) {
    ASSERT(framebuffer != nullptr);
    return framebuffer != nullptr ? framebuffer->color_texture : 0;
}

uint32_t framebuffer_get_depth_texture(const Framebuffer *framebuffer) {
    ASSERT(framebuffer != nullptr);
    return framebuffer != nullptr ? framebuffer->depth_texture : 0;
}

uint32_t framebuffer_get_width(const Framebuffer *framebuffer) {
    ASSERT(framebuffer != nullptr);
    return framebuffer != nullptr ? framebuffer->width : 0;
}

uint32_t framebuffer_get_height(const Framebuffer *framebuffer) {
    ASSERT(framebuffer != nullptr);
    return framebuffer != nullptr ? framebuffer->height : 0;
}

Result framebuffer_resolve_to(const Framebuffer *src, const Framebuffer *dst) {
    if (src == nullptr || dst == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }
    if (src->width != dst->width || src->height != dst->height) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    GLbitfield mask = 0;
    if (src->color_texture != 0 && dst->color_texture != 0) {
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (src->depth_texture != 0 && dst->depth_texture != 0) {
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    if (mask == 0) {
        return RESULT_ERROR_INVALID_STATE;
    }

    glBlitNamedFramebuffer(src->gl_fbo, dst->gl_fbo, 0, 0, (GLint)src->width, (GLint)src->height, 0,
                            0, (GLint)dst->width, (GLint)dst->height, mask, GL_NEAREST);
    return RESULT_OK;
}
