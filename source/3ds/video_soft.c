#include "vb_dsp.h"
#include "vb_set.h"
#include "v810_mem.h"

typedef union {
    struct {
        uint16_t mask[8];
        uint16_t colmask[3][4][8];
    } u16;
    struct {
        uint32_t mask[4];
        uint32_t colmask[3][4][4];
    } u32;
} CachedTile;

CachedTile tileCache[2048];

C3D_Tex screenTexSoft[2];

void video_soft_init(void) {
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

void update_texture_cache_soft(void) {
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
                for (int l = 0; l < 4; l++) {
                    const uint32_t cols[4] = {0, 0x55555555, 0xaaaaaaaa, 0xffffffff};
                    tileCache[t].u32.colmask[k][l][i] = colmask[k] & cols[l];
                }
            }
            tileCache[t].u32.mask[i] = ~(colmask[0] | colmask[1] | colmask[2]);
        }
    }
}

void video_soft_render(int alt_buf) {
    tDSPCACHE.DDSPDataState[alt_buf] = CPU_WROTE;
    for (int eye = 0; eye < 2; eye++) {
        uint16_t *fb = (uint16_t*)(V810_DISPLAY_RAM.pmemory + 0x10000 * eye + 0x8000 * alt_buf);
        memset(fb, 0, 0x8000);
	    u16 *windows = (u16 *)(V810_DISPLAY_RAM.pmemory + 0x3d800);
        for (int wnd = 31; wnd >= 0; wnd--) {
            if (windows[wnd * 16] & 0x40)
                break;
            if (!(windows[wnd * 16] & 0xc000))
                continue;
            
            if ((windows[wnd * 16] & 0x3000) != 0x3000) {
                // background world
                uint8_t mapid = windows[wnd * 16] & 0xf;
                uint8_t scx_pow = ((windows[wnd * 16] >> 10) & 3);
                uint8_t scy_pow = ((windows[wnd * 16] >> 8) & 3);
                uint8_t scx = 1 << scx_pow;
                uint8_t scy = 1 << scy_pow;
                bool over = windows[wnd * 16] & 0x80;
                int16_t base_gx = (s16)(windows[wnd * 16 + 1] << 6) >> 6;
                int16_t gp = (s16)(windows[wnd * 16 + 2] << 6) >> 6;
                int16_t gy = windows[wnd * 16 + 3];
                int16_t base_mx = (s16)(windows[wnd * 16 + 4] << 3) >> 3;
                int16_t mp = (s16)(windows[wnd * 16 + 5] << 1) >> 1;
                int16_t my = (s16)(windows[wnd * 16 + 6] << 3) >> 3;
                int16_t w = windows[wnd * 16 + 7] + 1;
                int16_t h = windows[wnd * 16 + 8] + 1;
                int16_t over_tile = windows[wnd * 16 + 10] & 0x7ff;

                u16 *tilemap = (u16 *)(V810_DISPLAY_RAM.pmemory + 0x20000);

                if ((windows[wnd * 16] & 0x3000) == 0) {
                    // normal world
                    int mx = base_mx + (eye == 0 ? -mp : mp);
                    int gx = base_gx + (eye == 0 ? -gp : gp);

                    bool over_visible = !over || tileVisible[tilemap[over_tile] & 0x07ff];

                    int tsy = my >> 3;
                    int mapsy = tsy >> 6;
                    tsy &= 63;
                    if (!over) {
                        mapsy &= scy - 1;
                    }

                    if ((gy & 7) || (my & 7)) {
                        dprintf(0, "unaligned tiles not supported in software rendering\n");
                    }

                    for (int x = 0; x < w; x++) {
                        if (gx + x < 0) continue;
                        if (gx + x >= 384) break;
                        int bpx = (mx + x) & 7;
                        int tx = (mx + x) >> 3;
                        int mapx = tx >> 6;
                        tx &= 63;
                        if (!over) mapx &= scx - 1;

                        int ty = tsy;
                        int mapy = mapsy;
                        int current_map = mapid + scx * mapy + mapx;
                        for (int y = 0; y < h; y += 8) {
                            if (gy + y < 0) continue;
                            if (gy + y >= 224) break;
                            bool use_over = over && ((mapx & (scx - 1)) != mapx || (mapy & (scy - 1)) != mapy);
							uint16_t tile = tilemap[use_over ? over_tile : (64 * 64) * current_map + 64 * ty + tx];
                            if (++ty >= 64) {
                                ty = 0;
                                if ((++mapy & (scy - 1)) == 0 && !over) mapy = 0;
								current_map = mapid + scx * mapy + mapx;
                            }
							uint16_t tileid = tile & 0x07ff;
							if (!tileVisible[tileid]) continue;
                            int palette = tile >> 14;
                            int px = tile & 0x2000 ? 7 - bpx : bpx;
                            int colors[] = {0, 0b0101010101010101, 0b1010101010101010, 0xffff};
                            int value =
                                (tileCache[tileid].u16.colmask[0][(tVIPREG.GPLT[palette] >> 2) & 3][px]) |
                                (tileCache[tileid].u16.colmask[1][(tVIPREG.GPLT[palette] >> 4) & 3][px]) |
                                (tileCache[tileid].u16.colmask[2][(tVIPREG.GPLT[palette] >> 6) & 3][px]);
                            if (tile & 0x1000) {
                                value = __builtin_bswap16(value);
                                value = ((value & 0xf0f0) >> 4) | ((value << 4) & 0xf0f0);
                                value = ((value & 0xcccc) >> 2) | ((value << 2) & 0xcccc);
                            }
                            fb[((gy + y) >> 3) + ((gx + x) * 256 / 8)] = value;
                        }
                    }
                }
            }
        }
    }
}

void video_soft_to_texture(int alt_buf) {
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
    int start_eye = eye_count == 2 ? 0 : tVBOpt.DEFAULT_EYE;
    for (int eye = start_eye; eye < start_eye + eye_count; eye++) {
        for (int tx = 0; tx < 384 / 8; tx++) {
            uint32_t *column_ptr = &out_fb[8 * 8 / 4 * 2 * (eye * 256 / 8 + 512 / 8 * (512 / 8 - 1 - tx))];
            int ymin, ymax;
            if (tVBOpt.RENDERMODE < 2) {
                SOFTBOUND *column = &tDSPCACHE.SoftBufWrote[alt_buf][tx];
                ymin = column->min;
                ymax = column->max;
                if (ymin > ymax) {
                    memset(column_ptr, 0, 8 * 8 * 2 * (224 / 8));
                    continue;
                }
                memset(column_ptr, 0, 8 * 8 * 2 * ymin);
                if (++ymax > 224 / 8) ymax = 224 / 8;
            } else {
                ymin = 0;
                ymax = 224 / 8;
            }

            for (int ty = ymin; ty < ymax; ty++) {
                uint32_t *out_tile = &column_ptr[8 * 8 / 4 * 2 * (1 + ty)];
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
	
                    // black, red, green, blue
                    const static uint16_t colors[4] = {0, 0xf00f, 0x0f0f, 0x00ff};

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
            memset(column_ptr + 8 * 8 / 4 * 2 * ymax, 0, 8 * 8 * 2 * (224 / 8 - ymax));
        }
    }
}