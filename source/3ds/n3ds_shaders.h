#ifndef N3DS_SHADERS_H
#define N3DS_SHADERS_H

#include <3ds/gpu/shaderProgram.h>

struct n3ds_uniform_locations {
    int posscale,
        pal1tex, pal2tex, pal3col,
        offset,
        bgmap_offsets,
        shading_offset;
};

extern struct n3ds_uniform_locations uLoc;
extern shaderProgram_s sAffine, sChar, sFinal;

void n3ds_shaders_init(void);

#endif
