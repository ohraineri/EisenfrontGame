/*
 * Skybox - a cubemap rendered as an infinitely distant background.
 *
 * The vertex shader forces every vertex's clip-space z to equal w
 * (gl_Position = clip.xyww), which after the perspective divide always
 * gives NDC depth exactly 1.0 - the far plane - regardless of the
 * cube's actual size, and the view matrix's translation is stripped
 * before use so the cube never appears to move as the camera moves
 * (only rotating the camera rotates which face you see, exactly like a
 * real skybox). Draw the skybox after all opaque geometry with
 * GL_LEQUAL depth testing (skybox_render sets this and restores
 * GL_LESS afterward) so it only appears where nothing closer was drawn.
 *
 * Dependencies: Core, Graphics Context, Buffers, Texture, cglm.
 */
#ifndef EISENFRONT_SKYBOX_H
#define EISENFRONT_SKYBOX_H

#include "buffers.h"
#include "core.h"
#include "graphics_context.h"
#include "texture.h"

#include <cglm/cglm.h>

typedef struct Skybox Skybox;

/* Retains cubemap; caller keeps its own reference too if it still needs
 * one (standard retain-counted ownership, like Material's textures). */
ENGINE_API Result skybox_create(Texture *cubemap, Skybox **out_skybox);
ENGINE_API void   skybox_destroy(Skybox *skybox);

ENGINE_API void skybox_render(const Skybox *skybox, mat4 view, mat4 projection);

#endif /* EISENFRONT_SKYBOX_H */
