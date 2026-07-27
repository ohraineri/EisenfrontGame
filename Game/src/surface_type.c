#include "surface_type.h"

const char *surface_type_name(SurfaceType surface_type) {
    switch (surface_type) {
        case SURFACE_TYPE_SOIL:
            return "Soil";
        case SURFACE_TYPE_SAND:
            return "Sand";
        case SURFACE_TYPE_GRAVEL:
            return "Gravel";
        case SURFACE_TYPE_WOOD:
            return "Wood";
        case SURFACE_TYPE_METAL:
            return "Metal";
        case SURFACE_TYPE_CONCRETE:
            return "Concrete";
        case SURFACE_TYPE_COUNT:
        default:
            return "Unknown";
    }
}
