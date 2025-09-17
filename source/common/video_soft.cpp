#include "vb_dsp.h"
#include "v810_mem.h"

static struct {
    // half-nibbles are colour indices
    union {
        uint16_t u16[8];
        uint32_t u32[4];
    } indices;
    // only one index, for each colour
    struct {
        struct {
            struct {
                uint16_t shade[4];
            } col[4]; // actually 3 but 4 is better aligned
        } column[8];
    } colmask;
    // mask where transparent pixels are 1
    union {
        uint16_t u16[8];
        uint32_t u32[4];
    } mask;
} tileCache[2048];

void update_texture_cache_soft(void) {
    for (int t = 0; t < 2048; t++) {
		// skip if this tile wasn't modified
		if (tDSPCACHE.CharacterCache[t])
			tDSPCACHE.CharacterCache[t] = false;
		else
			continue;

		uint32_t *tile = (uint32_t*)(vb_state->V810_DISPLAY_RAM.off + ((t & 0x600) << 6) + 0x6000 + (t & 0x1ff) * 16);

		// optimize invisible tiles
		{
			bool tv = ((uint64_t*)tile)[0] | ((uint64_t*)tile)[1];
			tileVisible[t] = tv;
			if (!tv) {
                memset(&tileCache[t].indices, 0, sizeof(tileCache[t].indices));
                memset(&tileCache[t].colmask, 0, sizeof(tileCache[t].colmask));
                memset(&tileCache[t].mask, -1, sizeof(tileCache[t].mask));
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
            tileCache[t].indices.u32[i] = column;
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
                    uint32_t columns = colmask[k] & cols[l];
                    tileCache[t].colmask.column[i * 2].col[k].shade[l] = columns;
                    tileCache[t].colmask.column[i * 2 + 1].col[k].shade[l] = columns >> 16;
                }
            }
            tileCache[t].mask.u32[i] = ~(colmask[0] | colmask[1] | colmask[2]);
        }
    }
}

static uint16_t get_tile_column(int tileid, uint16_t pal, int x, bool yflip) {
    int value =
        (tileCache[tileid].colmask.column[x].col[0].shade[(pal >> 2) & 3]) |
        (tileCache[tileid].colmask.column[x].col[1].shade[(pal >> 4) & 3]) |
        (tileCache[tileid].colmask.column[x].col[2].shade[(pal >> 6) & 3]);
    if (yflip) {
        value = __builtin_bswap16(value);
        value = ((value & 0xf0f0) >> 4) | ((value << 4) & 0xf0f0);
        value = ((value & 0xcccc) >> 2) | ((value << 2) & 0xcccc);
    }
    return value;
}

static uint16_t get_tile_mask(int tileid, int x, bool yflip) {
    int value = tileCache[tileid].mask.u16[x];
    if (yflip) {
        value = __builtin_bswap16(value);
        value = ((value & 0xf0f0) >> 4) | ((value << 4) & 0xf0f0);
        value = ((value & 0xcccc) >> 2) | ((value << 2) & 0xcccc);
    }
    return value;
}

template<bool aligned, bool over> void render_normal_world(uint16_t *fb, WORLD *world, int eye, int drawn_fb) {
    uint8_t mapid = world->head & 0xf;
    uint8_t scx_pow = ((world->head >> 10) & 3);
    uint8_t scy_pow = ((world->head >> 8) & 3);
    uint8_t scx = 1 << scx_pow;
    uint8_t scy = 1 << scy_pow;
    int16_t base_gx = (s16)(world->gx << 6) >> 6;
    int16_t gp = (s16)(world->gp << 6) >> 6;
    int16_t gy = world->gy;
    int16_t base_mx = (s16)(world->mx << 3) >> 3;
    int16_t mp = (s16)(world->mp << 1) >> 1;
    int16_t my = (s16)(world->my << 3) >> 3;
    int16_t w = world->w + 1;
    int16_t h = world->h + 1;
    int16_t over_tile = world->over & 0x7ff;

    u16 *tilemap = (u16 *)(vb_state->V810_DISPLAY_RAM.off + 0x20000);

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

    u8 *gplt = vb_state->tVIPREG.GPLT;

    for (int x = 0; likely(x < w); x++) {
        if (unlikely(gx + x < 0)) continue;
        if (unlikely(gx + x >= 384)) break;
        int bpx = (mx + x) & 7;
        int tx = (mx + x) >> 3;
        int mapx = tx >> 6;
        tx &= 63;
        if (!over) mapx &= scx - 1;

        uint16_t *column_out = &fb[(gx + x) * 256 / 8];

        int ty = tsy;
        int mapy = mapsy;
        int current_map = mapid + scx * mapy + mapx;

        uint16_t prev_out = 0;
        uint16_t prev_mask = 0xffff >> (16 - gy_shift);

        for (int y = gy - (my & 7); likely(y < gy + h); y += 8) {
            if (unlikely(y >= 224)) break;
            bool use_over = over && ((mapx & (scx - 1)) != mapx || (mapy & (scy - 1)) != mapy);
            uint16_t tile = tilemap[use_over ? over_tile : (64 * 64) * current_map + 64 * ty + tx];
            if (++ty >= 64) {
                ty = 0;
                if ((++mapy & (scy - 1)) == 0 && !over) mapy = 0;
                current_map = mapid + scx * mapy + mapx;
            }
            if (unlikely(y <= -8)) continue;
            uint16_t tileid = tile & 0x07ff;
            if (!tileVisible[tileid] && (aligned || prev_mask == -1 >> (16 - gy_shift))) continue;
            int palette = tile >> 14;
            int px = tile & 0x2000 ? 7 - bpx : bpx;
            int value = get_tile_column(tileid, gplt[palette], px, (tile & 0x1000) != 0);
            uint16_t mask = get_tile_mask(tileid, px, (tile & 0x1000) != 0);
            uint16_t current_out, current_mask;
            if (aligned) {
                current_out = value;
                current_mask = mask;
                if (mask == 0xffff) continue;
            } else {
                current_out = ((value << gy_shift)) | prev_out;
                current_mask = ((mask << gy_shift)) | prev_mask;
                prev_out = (value) >> (16 - gy_shift);
                prev_mask = (mask) >> (16 - gy_shift);
                if (unlikely(y < gy)) {
                    current_mask |= 0xffff >> ((gy & 7) * 2);
                    current_out &= ~current_mask;
                }
            }
            if (unlikely(y < 0)) continue;
            uint16_t *out_word = &column_out[y >> 3];
            *out_word = (*out_word & current_mask) | current_out;
        }
        if (((gy & 7) + h) >= 8 && ((gy + h) & 7) != 0) {
            uint16_t current_out = prev_out;
            uint16_t current_mask = (-1 << gy_shift) | prev_mask;
            uint16_t *out_word = &column_out[(gy - (my & 7) + h) >> 3];
            *out_word = (*out_word & current_mask) | current_out;
        }
    }
}

template<bool over> void render_affine_world(WORLD *world, int drawn_fb) {
    uint8_t mapid = world->head & 0xf;
    uint8_t scx_pow = ((world->head >> 10) & 3);
    uint8_t scy_pow = ((world->head >> 8) & 3);
    uint8_t scx = 1 << scx_pow;
    uint8_t scy = 1 << scy_pow;
    int scx_scy_mask = (scx - 1) | ((scy - 1) << 16);
    int16_t base_gx = (s16)(world->gx << 6) >> 6;
    int16_t gp = (s16)(world->gp << 6) >> 6;
    int16_t gy = world->gy;
    int16_t base_mx = (s16)(world->mx << 3) >> 3;
    int16_t mp = (s16)(world->mp << 1) >> 1;
    int16_t my = (s16)(world->my << 3) >> 3;
    int16_t w = world->w + 1;
    int16_t h = world->h + 1;
    int16_t over_tile = world->over & 0x7ff;

    u16 *tilemap = (u16 *)(vb_state->V810_DISPLAY_RAM.off + 0x20000);

    u16 param_base = world->param;
    s16 *params = (s16 *)(vb_state->V810_DISPLAY_RAM.off + 0x20000 + param_base * 2);

    u8 *gplt = vb_state->tVIPREG.GPLT;

    for (int eye = 0; eye < 2; eye++) {
        if (!(world->head & (0x8000 >> eye)))
            continue;

        uint16_t *fb = (uint16_t*)(vb_state->V810_DISPLAY_RAM.off + 0x10000 * eye + 0x8000 * drawn_fb);

        int mx = base_mx + (eye == 0 ? -mp : mp);
        int gx = base_gx + (eye == 0 ? -gp : gp);
        for (int y = 0; likely(y < h); y++) {
            if (unlikely(gy + y < 0)) continue;
            if (unlikely(gy + y >= 224)) break;
            int mx = params[y * 8 + 0] << 6;
            s16 mp = params[y * 8 + 1];
            int my = params[y * 8 + 2] << 6;
            s32 dx = params[y * 8 + 3];
            s32 dy = params[y * 8 + 4];
            mx += (mp >= 0 ? mp * eye : -mp * !eye) * dx;
            my += (mp >= 0 ? mp * eye : -mp * !eye) * dy;

            int shift = (((gy + y) & 3) * 2);

            u8 *out_word = &((uint8_t*)(&fb[gx * 256 / 8]))[((gy + y) >> 2)];
            u8 *end = out_word + w * 256 / 4;
            if (gx < 0) {
                mx += dx * -gx;
                my += dy * -gx;
                out_word += -gx * 256 / 4;
            }
            if (gx + w > 384) {
                end = ((uint8_t*)fb) + 0x6000;
            }

            for (; likely(out_word < end); out_word += 256 / 4) {
                if (true) {
                    // storing xmap and ymap in one int here lets us mask/compare with scx/scy in one go,
                    // which is slightly faster than storing them separately
                    int xmap = mx >> (9 + 9);
                    int ymap = my >> (9 + 9);
                    int xmap_ymap = xmap | (ymap << 16);
                    int xmap_ymap_masked = xmap_ymap & scx_scy_mask;
                    int tx = (mx >> (9 + 3)) & 63;
                    // premultiplied by 64
                    int ty_scaled = (my >> (9 + 3 - 6)) & (63 << 6);
                    // note: not doubled because that doesn't help
                    int bpx = (mx >> 9) & 7;
                    // note: this is doubled because that does help
                    int dbpy = (my >> 8) & (7 << 1);
                    int tile_pos;
                    if (over && unlikely(xmap_ymap != xmap_ymap_masked)) {
                        tile_pos = over_tile;
                    } else {
                        int this_map = mapid + (xmap_ymap_masked >> 16) * scx + (xmap_ymap_masked & 0xffff);
                        tile_pos = this_map * 4096 + ty_scaled + tx;
                    }
                    u16 tile = tilemap[tile_pos];
                    u16 tileid = tile & 0x07ff;
                    int palette = tile >> 14;
                    int px = tile & 0x2000 ? 7 - bpx : bpx;
                    int dpy = tile & 0x1000 ? (7 << 1) - dbpy : dbpy;
                    uint16_t tilecolumn = tileCache[tileid].indices.u16[px];
                    int pxindex = (tilecolumn >> dpy) & 3;
                    if (pxindex) {
                        int pxvalue = (gplt[palette] >> (pxindex * 2)) & 3;
                        *out_word = (*out_word & ~(3 << shift)) | (pxvalue << shift);
                    }
                }
                mx += dx;
                my += dy;
            }
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
        uint16_t *fb = (uint16_t*)(vb_state->V810_DISPLAY_RAM.off + 0x10000 * eye + 0x8000 * drawn_fb);
        memset(fb, 0, 0x6000);
    }
    WORLD *worlds = (WORLD *)(vb_state->V810_DISPLAY_RAM.off + 0x3d800);
    for (int wrld = 31; wrld >= 0; wrld--) {
        if (worlds[wrld].head & 0x40)
            break;
        if (!(worlds[wrld].head & 0xc000))
            continue;

        // set softbuf modified area for background worlds
        if ((worlds[wrld].head & 0x3000) != 0x3000) {
            int16_t base_gx = (s16)(worlds[wrld].gx << 6) >> 6;
            int16_t gp = (s16)(worlds[wrld].gp << 6) >> 6;
            int16_t gy = worlds[wrld].gy;
            int16_t w = worlds[wrld].w + 1;
            int16_t h = worlds[wrld].h + 1;
            int left_gx = base_gx - gp;
            int right_gx = base_gx + gp;
            int min_gx, max_gx;
            if ((worlds[wrld].head & 0xc000) == 0xc000) {
                min_gx = left_gx < right_gx ? left_gx : right_gx;
                max_gx = left_gx > right_gx ? left_gx : right_gx;
            } else {
                min_gx = max_gx = worlds[wrld].head & 0x8000 ? left_gx : right_gx;
            }
            for (int x = min_gx & ~7; x < max_gx + abs(gp) + w && x < 384; x += 8) {
                if (x < 0) continue;
                SOFTBOUND *column = &tDSPCACHE.SoftBufWrote[drawn_fb][x / 8];
                int min = gy / 8;
                if (min < 0) min = 0;
                if (column->min > min) column->min = min;
                int max = (gy + h - 1) / 8;
                if (max < 0) max = 0;
                if (max > 31) max = 31;
                if (column->max < max) column->max = max;
            }
        }
        
        if ((worlds[wrld].head & 0x3000) == 0) {
            // normal world
            for (int eye = 0; eye < 2; eye++) {
                if (!(worlds[wrld].head & (0x8000 >> eye)))
                    continue;
                uint16_t *fb = (uint16_t*)(vb_state->V810_DISPLAY_RAM.off + 0x10000 * eye + 0x8000 * drawn_fb);
                int16_t gy = worlds[wrld].gy;
                int16_t my = (s16)(worlds[wrld].my << 3) >> 3;
                int16_t h = worlds[wrld].h + 1;
                bool over = worlds[wrld].head & 0x80;
                if ((gy & 7) || (my & 7) || (h & 7)) {
                    if (over)
                        render_normal_world<false, true>(fb, &worlds[wrld], eye, drawn_fb);
                    else
                        render_normal_world<false, false>(fb, &worlds[wrld], eye, drawn_fb);
                } else {
                    if (over)
                        render_normal_world<true, true>(fb, &worlds[wrld], eye, drawn_fb);
                    else
                        render_normal_world<true, false>(fb, &worlds[wrld], eye, drawn_fb);
                }
            }
        } else if ((worlds[wrld].head & 0x3000) == 0x1000) {
            // h-bias world
            // TODO
        } else if ((worlds[wrld].head & 0x3000) == 0x2000) {
            // affine world
            bool over = worlds[wrld].head & 0x80;
            if (over) {
                render_affine_world<true>(&worlds[wrld], drawn_fb);
            } else {
                render_affine_world<false>(&worlds[wrld], drawn_fb);
            }
        } else {
            // object world
            int start_index = object_group_id == 0 ? 1023 : (vb_state->tVIPREG.SPT[object_group_id - 1]) & 1023;
            int end_index = vb_state->tVIPREG.SPT[object_group_id] & 1023;
            for (int i = end_index; i != start_index; i = (i - 1) & 1023) {
                u16 *obj_ptr = (u16 *)(vb_state->V810_DISPLAY_RAM.off + 0x0003E000 + 8 * i);

                u16 cw3 = obj_ptr[3];
                u16 tileid = cw3 & 0x07ff;
                if (!tileVisible[tileid]) continue;

                u16 base_x = obj_ptr[0];
                u16 cw1 = obj_ptr[1];
                s16 y = *(u8*)&obj_ptr[2];
                if (y > 224) y = (s8)y;

                short palette = (cw3 >> 14);

                s16 jp = (s16)(cw1 << 6) >> 6;

                for (int x = (base_x - abs(jp)) & ~7; x < base_x + abs(jp) && x < 384; x += 8) {
                    if (x < 0) continue;
                    SOFTBOUND *column = &tDSPCACHE.SoftBufWrote[drawn_fb][x / 8];
                    int min = y / 8;
                    if (min < 0) min = 0;
                    if (column->min > min) column->min = min;
                    int max = (y + 7) / 8;
                    if (max < 0) max = 0;
                    if (max > 31) max = 31;
                    if (column->max < max) column->max = max;
                }

                for (int eye = 0; eye < 2; eye++) {
                    if (!(cw1 & (0x8000 >> eye)))
                        continue;

                    uint16_t *fb = (uint16_t*)(vb_state->V810_DISPLAY_RAM.off + 0x10000 * eye + 0x8000 * drawn_fb);

                    s16 x = base_x;
                    if (eye == 0)
                        x -= jp;
                    else
                        x += jp;
                    
                    for (int bpx = 0; bpx < 8; bpx++) {
                        if (x + bpx < 0) continue;
                        if (x + bpx >= 384) break;
                        int px = cw3 & 0x2000 ? 7 - bpx : bpx;
                        int value = get_tile_column(tileid, vb_state->tVIPREG.JPLT[palette], px, (cw3 & 0x1000) != 0);
                        uint16_t mask = get_tile_mask(tileid, px, (cw3 & 0x1000) != 0);
                        if (mask == 0xffff) continue;
                        
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