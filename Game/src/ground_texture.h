/*
 * Procedural dirt/gravel ground texture - generated entirely in code
 * (value noise, no source image), per the vertical slice's "no
 * external art" decision. See ground_texture.c.
 */
#ifndef OUTPOST_GROUND_TEXTURE_H
#define OUTPOST_GROUND_TEXTURE_H

#include "eisenfront/texture.h"

Result ground_texture_create(uint32_t size, uint32_t seed, Texture **out_texture);

#endif /* OUTPOST_GROUND_TEXTURE_H */
