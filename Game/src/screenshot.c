#include "screenshot.h"

#include <glad/glad.h>

#include <stdlib.h>
#include <string.h>

#include <stb_image_write.h>

Result screenshot_capture(int32_t width, int32_t height, const char *path) {
    if (width <= 0 || height <= 0 || path == nullptr) {
        return RESULT_ERROR_INVALID_ARGUMENT;
    }

    const size_t row_stride = (size_t)width * 3u;
    uint8_t     *pixels = malloc(row_stride * (size_t)height);
    if (pixels == nullptr) {
        return RESULT_ERROR_OUT_OF_MEMORY;
    }

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels);

    /* glReadPixels' origin is the bottom-left corner; PNG rows go
     * top-down, so flip in place before writing. */
    uint8_t *row_buffer = malloc(row_stride);
    if (row_buffer != nullptr) {
        for (int32_t y = 0; y < height / 2; ++y) {
            uint8_t *top = pixels + (size_t)y * row_stride;
            uint8_t *bottom = pixels + (size_t)(height - 1 - y) * row_stride;
            memcpy(row_buffer, top, row_stride);
            memcpy(top, bottom, row_stride);
            memcpy(bottom, row_buffer, row_stride);
        }
        free(row_buffer);
    }

    const int write_ok = stbi_write_png(path, width, height, 3, pixels, (int)row_stride);
    free(pixels);
    return write_ok != 0 ? RESULT_OK : RESULT_ERROR_IO;
}
