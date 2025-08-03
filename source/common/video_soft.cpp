#include "vb_dsp.h"
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
			if (!tv) {
                for (int i = 0; i < 4; i++) {
                    tileCache[t].u32.mask[i] = -1;
                    for (int j = 0; j < 3; j++) {
                        for (int k = 0; k < 4; k++) {
                            tileCache[t].u32.colmask[j][i][k] = 0;
                        }
                    }
                }
                continue;
            }
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
            tmp = (column & (~(column & 0x55555555) << 1)) & 0xaaaaaaaa;
            colmask[1] = tmp | (tmp >> 1);
            tmp = ((column & 0xaaaaaaaa) >> 1) & column;
            colmask[2] = tmp | (tmp << 1);
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

static uint16_t get_tile_column(int tileid, uint16_t pal, int x, bool yflip) {
    int value =
        (tileCache[tileid].u16.colmask[0][(pal >> 2) & 3][x]) |
        (tileCache[tileid].u16.colmask[1][(pal >> 4) & 3][x]) |
        (tileCache[tileid].u16.colmask[2][(pal >> 6) & 3][x]);
    if (yflip) {
        value = __builtin_bswap16(value);
        value = ((value & 0xf0f0) >> 4) | ((value << 4) & 0xf0f0);
        value = ((value & 0xcccc) >> 2) | ((value << 2) & 0xcccc);
    }
    return value;
}

static uint16_t get_tile_mask(int tileid, int x, bool yflip) {
    int value = tileCache[tileid].u16.mask[x];
    if (yflip) {
        value = __builtin_bswap16(value);
        value = ((value & 0xf0f0) >> 4) | ((value << 4) & 0xf0f0);
        value = ((value & 0xcccc) >> 2) | ((value << 2) & 0xcccc);
    }
    return value;
}

template<bool aligned> void render_normal_world(uint16_t *fb, WORLD *world, int eye, int drawn_fb) {
    uint8_t mapid = world->head & 0xf;
    uint8_t scx_pow = ((world->head >> 10) & 3);
    uint8_t scy_pow = ((world->head >> 8) & 3);
    uint8_t scx = 1 << scx_pow;
    uint8_t scy = 1 << scy_pow;
    bool over = world->head & 0x80;
    int16_t base_gx = (s16)(world->gx << 6) >> 6;
    int16_t gp = (s16)(world->gp << 6) >> 6;
    int16_t gy = world->gy;
    int16_t base_mx = (s16)(world->mx << 3) >> 3;
    int16_t mp = (s16)(world->mp << 1) >> 1;
    int16_t my = (s16)(world->my << 3) >> 3;
    int16_t w = world->w + 1;
    int16_t h = world->h + 1;
    int16_t over_tile = world->over & 0x7ff;

    u16 *tilemap = (u16 *)(V810_DISPLAY_RAM.pmemory + 0x20000);

    int mx = base_mx + (eye == 0 ? -mp : mp);
    int gx = base_gx + (eye == 0 ? -gp : gp);

    bool over_visible = !over || tileVisible[tilemap[over_tile] & 0x07ff];

    int tsy = my >> 3;
    int mapsy = tsy >> 6;
    tsy &= 63;
    if (!over) {
        mapsy &= scy - 1;
    }

    uint8_t gy_shift = ((gy - my) & 7) * 2;
    uint8_t my_shift = (my & 7) * 2;

    for (int x = gx; x < gx + w && x < 384; x += 8) {
        if (x < 0) continue;
        SOFTBOUND *column = &tDSPCACHE.SoftBufWrote[drawn_fb][x / 8];
        int min = gy / 8;
        if (min < 0) min = 0;
        if (column->min > min) column->min = min;
        int max = (gy + h) / 8;
        if (max < 0) max = 0;
        if (max > 31) max = 31;
        if (column->max < max) column->max = max;
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

        uint16_t prev_out = 0;
        uint16_t prev_mask = -1;

        for (int y = gy - (my & 7); y < gy + h; y += 8) {
            if (y >= 224) break;
            bool use_over = over && ((mapx & (scx - 1)) != mapx || (mapy & (scy - 1)) != mapy);
            uint16_t tile = tilemap[use_over ? over_tile : (64 * 64) * current_map + 64 * ty + tx];
            if (++ty >= 64) {
                ty = 0;
                if ((++mapy & (scy - 1)) == 0 && !over) mapy = 0;
                current_map = mapid + scx * mapy + mapx;
            }
            if (y <= -8) continue;
            uint16_t tileid = tile & 0x07ff;
            if (!tileVisible[tileid] && (aligned || prev_mask == -1 >> (16 - gy_shift))) continue;
            int palette = tile >> 14;
            int px = tile & 0x2000 ? 7 - bpx : bpx;
            int value = get_tile_column(tileid, tVIPREG.GPLT[palette], px, (tile & 0x1000) != 0);
            uint16_t mask = get_tile_mask(tileid, px, (tile & 0x1000) != 0);
            uint16_t current_out, current_mask;
            if (aligned) {
                current_out = value;
                current_mask = mask;
            } else {
                current_out = ((value << gy_shift)) | prev_out;
                current_mask = ((mask << gy_shift)) | prev_mask;
                prev_out = (value) >> (16 - gy_shift);
                prev_mask = (mask) >> (16 - gy_shift);
            }
            if (y < 0) continue;
            uint16_t *out_word = &fb[((y) >> 3) + ((gx + x) * 256 / 8)];
            *out_word = (*out_word & current_mask) | current_out;
        }
        if ((gy + h) & 7) {
            uint16_t current_out = prev_out;
            uint16_t current_mask = (-1 << gy_shift) | prev_mask;
            uint16_t *out_word = &fb[((gy - (my & 7) + h) >> 3) + ((gx + x) * 256 / 8)];
            *out_word = (*out_word & current_mask) | current_out;
        }
    }
}

void video_soft_render(int drawn_fb) {
    tDSPCACHE.DDSPDataState[drawn_fb] = CPU_WROTE;
    #ifdef __3DS__
    uint32_t fb_size;
    uint32_t *out_fb = (uint32_t*)C3D_Tex2DGetImagePtr(&screenTexSoft[drawn_fb], 0, &fb_size);
    memset(out_fb, 0, fb_size);
    #endif
	    uint8_t object_group_id = 3;
    for (int eye = 0; eye < 2; eye++) {
        uint16_t *fb = (uint16_t*)(V810_DISPLAY_RAM.pmemory + 0x10000 * eye + 0x8000 * drawn_fb);
        memset(fb, 0, 0x6000);
    }
    WORLD *worlds = (WORLD *)(V810_DISPLAY_RAM.pmemory + 0x3d800);
    for (int wrld = 31; wrld >= 0; wrld--) {
        if (worlds[wrld].head & 0x40)
            break;
        if (!(worlds[wrld].head & 0xc000))
            continue;
        
        if ((worlds[wrld].head & 0x3000) == 0) {
            // normal world
            for (int eye = 0; eye < 2; eye++) {
                if (!(worlds[wrld].head & (0x8000 >> eye)))
                    continue;
                uint16_t *fb = (uint16_t*)(V810_DISPLAY_RAM.pmemory + 0x10000 * eye + 0x8000 * drawn_fb);
                int16_t gy = worlds[wrld].gy;
                int16_t my = (s16)(worlds[wrld].my << 3) >> 3;
                int16_t h = worlds[wrld].h + 1;
                if ((gy & 7) || (my & 7) || (h & 7)) {
                    render_normal_world<false>(fb, &worlds[wrld], eye, drawn_fb);
                } else {
                    render_normal_world<true>(fb, &worlds[wrld], eye, drawn_fb);
                }
            }
        } else if ((worlds[wrld].head & 0x3000) != 0x3000) {
            // h-bias or affine world
            uint8_t mapid = worlds[wrld].head & 0xf;
            uint8_t scx_pow = ((worlds[wrld].head >> 10) & 3);
            uint8_t scy_pow = ((worlds[wrld].head >> 8) & 3);
            uint8_t scx = 1 << scx_pow;
            uint8_t scy = 1 << scy_pow;
            bool over = worlds[wrld].head & 0x80;
            int16_t base_gx = (s16)(worlds[wrld].gx << 6) >> 6;
            int16_t gp = (s16)(worlds[wrld].gp << 6) >> 6;
            int16_t gy = worlds[wrld].gy;
            int16_t base_mx = (s16)(worlds[wrld].mx << 3) >> 3;
            int16_t mp = (s16)(worlds[wrld].mp << 1) >> 1;
            int16_t my = (s16)(worlds[wrld].my << 3) >> 3;
            int16_t w = worlds[wrld].w + 1;
            int16_t h = worlds[wrld].h + 1;
            int16_t over_tile = worlds[wrld].over & 0x7ff;

            u16 *tilemap = (u16 *)(V810_DISPLAY_RAM.pmemory + 0x20000);

            u16 param_base = worlds[wrld].param;
            s16 *params = (s16 *)(&V810_DISPLAY_RAM.pmemory[0x20000 + param_base * 2]);

            for (int eye = 0; eye < 2; eye++) {
                if (!(worlds[wrld].head & (0x8000 >> eye)))
                    continue;

                uint16_t *fb = (uint16_t*)(V810_DISPLAY_RAM.pmemory + 0x10000 * eye + 0x8000 * drawn_fb);

                int mx = base_mx + (eye == 0 ? -mp : mp);
                int gx = base_gx + (eye == 0 ? -gp : gp);

                if ((worlds[wrld].head & 0x3000) == 0x1000) {
                    // h-bias
                    // TODO
                } else {
                    // affine
                    for (int y = 0; y < h; y++) {
                        if (gy + y < 0) continue;
                        if (gy + y >= 224) break;
                        int mx = params[y * 8 + 0] << 6;
                        s16 mp = params[y * 8 + 1];
                        int my = params[y * 8 + 2] << 6;
                        s32 dx = params[y * 8 + 3];
                        s32 dy = params[y * 8 + 4];
                        mx += (mp >= 0 ? mp * eye : -mp * !eye) * dx;
                        my += (mp >= 0 ? mp * eye : -mp * !eye) * dy;

                        int shift = (((gy + y) & 3) * 2);

                        for (int x = gx; x < gx + w; x++) {
                            if (x >= 384) break;
                            if (x >= 0) {
                                int xmap = mx >> (9 + 9);
                                int ymap = my >> (9 + 9);
                                int tx = (mx >> (9 + 3)) & 63;
                                int ty = (my >> (9 + 3)) & 63;
                                int bpx = (mx >> 9) & 7;
                                int bpy = (my >> 9) & 7;
                                u16 tile;
                                if (over && ((xmap & (scx - 1)) != xmap || (ymap & (scy - 1)) != ymap)) {
                                    tile = tilemap[over_tile];
                                } else {
                                    int this_map = mapid + (ymap & (scy - 1)) * scx + (xmap & (scx - 1));
                                    tile = tilemap[this_map * 4096 + ty * 64 + tx];
                                }
                                u16 tileid = tile & 0x07ff;
                                int palette = tile >> 14;
                                int px = tile & 0x2000 ? 7 - bpx : bpx;
                                int py = tile & 0x1000 ? 7 - bpy : bpy;
		                        uint16_t *tiledata = (uint16_t*)(V810_DISPLAY_RAM.pmemory + ((tileid & 0x600) << 6) + 0x6000 + (tileid & 0x1ff) * 16);
                                uint16_t tilerow = tiledata[py];
                                int pxindex = (tilerow >> (px*2)) & 3;
                                if (pxindex) {
                                    int pxvalue = (tVIPREG.GPLT[palette] >> (pxindex*2)) & 3;
                                    uint8_t *out_word = &((uint8_t*)(&fb[x * 256 / 8]))[((gy + y) >> 2)];
                                    *out_word = (*out_word & ~(3 << shift)) | (pxvalue << shift);
                                }
                            }
                            mx += dx;
                            my += dy;
                        }
                    }
                }
            }
        } else {
            // object world

            int start_index = object_group_id == 0 ? 1023 : (tVIPREG.SPT[object_group_id - 1]) & 1023;
            int end_index = tVIPREG.SPT[object_group_id] & 1023;
            for (int i = end_index; i != start_index; i = (i - 1) & 1023) {
                u16 *obj_ptr = (u16 *)(&V810_DISPLAY_RAM.pmemory[0x0003E000 + 8 * i]);

                u16 cw3 = obj_ptr[3];
                u16 tileid = cw3 & 0x07ff;
                if (!tileVisible[tileid]) continue;

                u16 base_x = obj_ptr[0];
                u16 cw1 = obj_ptr[1];
                s16 y = *(u8*)&obj_ptr[2];
                if (y > 224) y = (s8)y;

                short palette = (cw3 >> 14);

                s16 jp = (s16)(cw1 << 6) >> 6;

                for (int eye = 0; eye < 2; eye++) {
                    if (!(cw1 & (0x8000 >> eye)))
                        continue;

                    uint16_t *fb = (uint16_t*)(V810_DISPLAY_RAM.pmemory + 0x10000 * eye + 0x8000 * drawn_fb);

                    s16 x = base_x;
                    if (eye == 0)
                        x -= jp;
                    else
                        x += jp;
                    
                    for (int bpx = 0; bpx < 8; bpx++) {
                        if (x + bpx < 0) continue;
                        if (x + bpx >= 384) break;
                        int px = cw3 & 0x2000 ? 7 - bpx : bpx;
                        int value = get_tile_column(tileid, tVIPREG.JPLT[palette], px, (cw3 & 0x1000) != 0);
                        uint16_t mask = get_tile_mask(tileid, px, (cw3 & 0x1000) != 0);
                        if (mask == -1) continue;
                        
                        uint16_t *out_word = &fb[((y) >> 3) + ((x + bpx) * 256 / 8)];
                        if (y >= 0) {
                            *out_word = (*out_word & ((mask << ((y & 7) * 2)) | ((u16)-1 >> (16 - (y & 7) * 2)))) | (value << ((y & 7) * 2));
                        }

                        if ((y & 7) && y < 224-8) {
                            out_word++;
                            *out_word = (*out_word & ((mask >> (16 - (y & 7) * 2) | (-1 << ((y & 7) * 2))))) | (value >> (16 - (y & 7) * 2));
                        }
                    }
                }
            }
            object_group_id = (object_group_id - 1) & 3;
        }
    }
}