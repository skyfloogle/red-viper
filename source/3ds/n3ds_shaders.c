#include "n3ds_shaders.h"

#include <stdio.h>
#include <3ds.h>

#include <yatt_shader_utils.h>
#include <yatt_error.h>

#include "n3ds_shader_shbin.h" // u8 n3ds_shader_shbin_end[], u8 n3ds_shader_shbin[], u32 n3ds_shader_shbin_size;

#define ARRAY_COUNT(arr_) (size_t) (sizeof(arr_) / sizeof(arr_[0]))

// Must match linking order of shlist file
enum {
   DVLE_AFFINE_G = 0,
   DVLE_AFFINE_V,
   DVLE_CHAR_G,
   DVLE_CHAR_V,
   DVLE_FINAL_G,
   DVLE_FINAL_V,
};

struct n3ds_uniform_locations uLoc;
shaderProgram_s sAffine, sChar, sFinal;

static YATT_ShaderBinary shader_binary = { n3ds_shader_shbin, &n3ds_shader_shbin_size, NULL };

static YATT_ShaderProgram shader_programs[] = {
    { &shader_binary, &sAffine, DVLE_AFFINE_G, DVLE_AFFINE_V, .gsh_stride = 3 },
    { &shader_binary, &sChar,   DVLE_CHAR_G,   DVLE_CHAR_V,   .gsh_stride = 0 },
    { &shader_binary, &sFinal,  DVLE_FINAL_G,  DVLE_FINAL_V,  .gsh_stride = 3 },
};

static YATT_ShaderUniform uniforms[] = {
    { .name = "posscale",       .loc_pointer = &uLoc.posscale       },
    { .name = "pal1tex",        .loc_pointer = &uLoc.pal1tex        },
    { .name = "pal2tex",        .loc_pointer = &uLoc.pal2tex        },
    { .name = "pal3col",        .loc_pointer = &uLoc.pal3col        },
    { .name = "offset",         .loc_pointer = &uLoc.offset         },
    { .name = "bgmap_offsets",  .loc_pointer = &uLoc.bgmap_offsets  },
    { .name = "shading_offset", .loc_pointer = &uLoc.shading_offset },
};

void n3ds_shaders_init(void)
{
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
    
    if (yatt_init_shader_binaries(&shader_binary, 1) != YATT_OK)
        svcBreak(USERBREAK_PANIC);

    if (yatt_init_shader_programs(shader_programs, ARRAY_COUNT(shader_programs)) != YATT_OK)
        svcBreak(USERBREAK_PANIC);
    
    if (yatt_parse_shader_uniforms(uniforms, shader_programs, ARRAY_COUNT(uniforms), ARRAY_COUNT(shader_programs)) != YATT_OK)
        svcBreak(USERBREAK_PANIC);
}
