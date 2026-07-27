/*
 * Screenshot capture - glReadPixels straight to a PNG. Used both for
 * --smoke-test's headless verification (no display for a human to look
 * at) and, later, the Editor debug overlay's screenshot button.
 */
#ifndef OUTPOST_SCREENSHOT_H
#define OUTPOST_SCREENSHOT_H

#include "eisenfront/core.h"

Result screenshot_capture(int32_t width, int32_t height, const char *path);

#endif /* OUTPOST_SCREENSHOT_H */
