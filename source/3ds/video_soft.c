#include "vb_dsp.h"
#include "v810_mem.h"

typedef union {
    struct {
        uint16_t mask[8];
        uint16_t colmask[3][8];
    } u16;
    struct {
        uint32_t mask[4];
        uint32_t colmask[3][4];
    } u32;
} CachedTile;

CachedTile tileCache[2048];

C3D_Tex screenTexSoft[2];

void video_soft_init() {
	C3D_TexInitParams params;
	params.width = 512;
	params.height = 512;
	params.format = GPU_RGBA4;
	params.type = GPU_TEX_2D;
	params.onVram = false;
	params.maxLevel = 0;
	for (int i = 0; i < 2; i++)
        C3D_TexInitWithParams(&screenTexSoft[i], NULL, params);
}

void update_texture_cache_soft() {
    for (int t = 0; t < 2048; t++) {
		// skip if this tile wasn't modified
		if (tDSPCACHE.CharacterCache[t])
			tDSPCACHE.CharacterCache[t] = false;
		else
			continue;

		uint32_t *tile = (uint32_t*)(V810_DISPLAY_RAM.pmemory + ((t & 0x600) << 6) + 0x6000 + (t & 0x1ff) * 16);

		// optimize invisible tiles
		{
			bool tv = ((uint64_t*)tile)[0] | ((uint64_t*)tile)[1];
			tileVisible[t] = tv;
			if (!tv) continue;
		}

        for (int i = 0; i < 4; i++) {
            uint32_t column = 0;
            for (int j = 0; j < 4; j++) {
                uint32_t row = tile[j];
                column |= (
                    (row & (0x03 << 4*i)) |
                    ((row & (0x0c << (4*i))) << 14) |
                    ((row & (0x030000 << (4*i))) >> 14) |
                    ((row & (0x0c0000) << 4*i))
                ) >> 4*i << 4*j;
            }
            uint32_t tmp;
            uint32_t colmask[3];
            tmp = (column & (~(column & 0xaaaaaaaa) >> 1)) & 0x55555555;
            colmask[0] = tmp | (tmp << 1);
            tmp = (column & (~(column & 0x55555555) >> 1)) & 0xaaaaaaaa;
            colmask[1] = tmp | (tmp >> 1);
            tmp = ((column & 0xaaaaaaaa) >> 1) & column;
            colmask[2] = tmp | (tmp >> 1);
            for (int k = 0; k < 3; k++) {
                tileCache[t].u32.colmask[k][i] = colmask[k];
            }
            tileCache[t].u32.mask[i] = ~(colmask[0] | colmask[1] | colmask[2]);
        }
    }
}

void video_soft_render(int alt_buf) {
    uint32_t fb_size;
    uint32_t *out_fb = C3D_Tex2DGetImagePtr(&screenTexSoft[alt_buf], 0, &fb_size);
    if (tDSPCACHE.DDSPDataState[alt_buf] == CPU_WROTE) {
        tDSPCACHE.DDSPDataState[alt_buf] = GPU_WROTE;
    } else {
        if (tDSPCACHE.DDSPDataState[alt_buf] == CPU_CLEAR) {
            tDSPCACHE.DDSPDataState[alt_buf] = GPU_CLEAR;
            memset(out_fb, 0, fb_size);
        }
        return;
    }
    // copy framebuffer
    for (int eye = 0; eye < eye_count; eye++) {
        for (int tx = 0; tx < 384 / 8; tx++) {
            for (int ty = 0; ty < 224 / 8; ty++) {
                uint32_t *out_tile = &out_fb[8 * 8 / 4 * 2 * (1 + eye * 256 / 8 + 512 / 8 * (512 / 8 - 1 - tx) + ty)];
                uint16_t *in_fb_ptr = (uint16_t*)(V810_DISPLAY_RAM.pmemory + 0x10000 * eye + 0x8000 * alt_buf + tx * (256 / 4 * 8) + ty * 2);

                for (int i = 0; i <= 2; i += 2) {
                    uint32_t slice1, slice2;
                    slice2 = *in_fb_ptr;
                    in_fb_ptr += 256 / 4 / 2;
                    slice2 |= (*in_fb_ptr << 16);
                    in_fb_ptr += 256 / 4 / 2;
                    slice1 = *in_fb_ptr;
                    in_fb_ptr += 256 / 4 / 2;
                    slice1 |= (*in_fb_ptr << 16);
                    in_fb_ptr += 256 / 4 / 2;
	
                    const static uint16_t colors[4] = {0, 0x00ff, 0x0f0f, 0xf00f};

                    #define SQUARE(x, i) { \
                        uint32_t left  = x >> (0 + 4*i) & 0x00030003; \
                        uint32_t right = x >> (2 + 4*i) & 0x00030003; \
                        *--out_tile = colors[(uint16_t)left] | (colors[(uint16_t)right] << 16); \
                        *--out_tile = colors[left >> 16] | (colors[right >> 16] << 16); \
                    }

                    SQUARE(slice2, 3);
                    SQUARE(slice2, 2);
                    SQUARE(slice1, 3);
                    SQUARE(slice1, 2);
                    SQUARE(slice2, 1);
                    SQUARE(slice2, 0);
                    SQUARE(slice1, 1);
                    SQUARE(slice1, 0);

                    #undef SQUARE
                }
            }
        }
    }
}