/*
 * Coarse per-surface classification for footstep audio (Volume II
 * Sec.25's SurfaceType field, scoped to exactly the materials Outpost's
 * level actually uses - see M9's pre-milestone declaration for what is
 * explicitly out of scope: per-foot events, wetness, indoor/outdoor
 * context, and gameplay-noise-as-a-server-event).
 */
#ifndef OUTPOST_SURFACE_TYPE_H
#define OUTPOST_SURFACE_TYPE_H

typedef enum SurfaceType {
    SURFACE_TYPE_SOIL = 0,
    SURFACE_TYPE_SAND,
    SURFACE_TYPE_GRAVEL,
    SURFACE_TYPE_WOOD,
    SURFACE_TYPE_METAL,
    SURFACE_TYPE_CONCRETE,
    SURFACE_TYPE_COUNT
} SurfaceType;

/* Never returns nullptr, including for an out-of-range value. */
const char *surface_type_name(SurfaceType surface_type);

#endif /* OUTPOST_SURFACE_TYPE_H */
