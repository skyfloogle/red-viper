#include <stdlib.h>
#include <string.h>

#include "vb_dsp.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_set.h"
#include "video_hard.h"

AffineCacheEntry tileMapCache[AFFINE_CACHE_SIZE];

vertex *vbuf, *vcur;
avertex *avbuf, *avcur;

uint16_t *rgba4_framebuffers;


void video_hard_init(void) {
	gpu_init();

	vbuf = linearAlloc(sizeof(vertex) * VBUF_SIZE);
	avbuf = linearAlloc(sizeof(avertex) * AVBUF_SIZE);

	rgba4_framebuffers = linearAlloc(384 * DOWNLOADED_FRAMEBUFFER_WIDTH * 2 * 2);

	gpu_set_opaque(false);
}

// returns vertex count
int render_affine_cache(int mapid, vertex *vbuf, vertex *vcur, int umin, int umax, int vmin, int vmax) {
	int vcount = 0;

	int cache_id = mapid % AFFINE_CACHE_SIZE;
	AffineCacheEntry *cache = &tileMapCache[cache_id];
	bool force_redraw = cache->bg == -1;
	int old_umin = cache->lumin;
	int old_umax = cache->lumax;
	int old_vmin = cache->lvmin;
	int old_vmax = cache->lvmax;
	// move the bounds only if newly used this frame or if bigger
	if (cache->umin > umin || !cache->used)
		cache->umin = umin;
	if (cache->umax < umax || !cache->used)
		cache->umax = umax;
	if (cache->vmin > vmin || !cache->used)
		cache->vmin = vmin;
	if (cache->vmax < vmax || !cache->used)
		cache->vmax = vmax;
	
	// increase old bounds if current bounds are bigger or if cache was forcefully invalidated
	if (cache->lumin > umin || force_redraw)
		cache->lumin = umin;
	if (cache->lumax < umax || force_redraw)
		cache->lumax = umax;
	if (cache->lvmin > vmin || force_redraw)
		cache->lvmin = vmin;
	if (cache->lvmax < vmax || force_redraw)
		cache->lvmax = vmax;

	// clear tile cache if force redraw
	if (force_redraw) {
		memset(cache->tiles, 0, sizeof(cache->tiles));
	}

	cache->bg = mapid;
	cache->used = true;

	int uumin = umin & 0x1f8;
	int vvmin = vmin & 0x1f8;
	int uumax = umax & 0x1f8;
	int vvmax = vmax & 0x1f8;

	if (umax - umin >= 512) {
		uumin = 0;
		uumax = 511;
	}
	if (vmax - vmin >= 512) {
		vvmin = 0;
		vvmax = 511;
	}

	bool uwrap = uumin > uumax;
	bool vwrap = vvmin > vvmax;

	int xstart = uwrap ? 0 : uumin;
	int ystart = vwrap ? 0 : vvmin;
	int xend = uwrap ? 511 : uumax;
	int yend = vwrap ? 511 : vvmax;

	u16 *tilemap = (u16 *)(vb_state->V810_DISPLAY_RAM.off + 0x20000 + 8192 * (mapid));
	for (int y = ystart; y <= yend; y += 8) {
		if (vwrap && !(y >= vvmin || y <= vvmax)) continue;
		bool new_row = force_redraw || (old_vmax - old_vmin < 512 && (y < (old_vmin & ~7) || y > old_vmax));
		for (int x = xstart; x <= xend; x += 8) {
			if (uwrap && !(x >= uumin || x <= uumax)) continue;
			uint16_t tile = tilemap[(y << 3) + (x >> 3)];
			uint16_t tileid = tile & 0x07ff;
			tile |= 0x800; // flag to indicate that tile was drawn since last clear

			if (!(new_row || (old_umax - old_umin < 512 && (x < (old_umin & ~7) || x > old_umax))
				|| cache->tiles[(y << 3) + (x >> 3)] != tile
				|| tDSPCACHE.CharacterCache[tileid]
				|| cache->GPLT[tile >> 14] != vb_state->tVIPREG.GPLT[tile >> 14]
			)) continue;
			cache->tiles[(y << 3) + (x >> 3)] = tile;

			short u = (tileid % 32);
			short v = (tileid / 32);

			if (vcur >= vbuf + VBUF_SIZE) {
				dprintf(0, "VBUF OVERRUN!\n");
				break;
			}

			vcur->x = x;
			vcur->y = y;
			vcur->u = u;
			vcur->v = v;
			vcur->palette = tile >> 14;
			vcur++->orient = (tile >> 12) & 3;

			vcount++;
		}
	}
	cache->visible = true;

	if (vcount == 0) {
		// bail
		return 0;
	}

	// set up cache texture
	gpu_target_affine(cache_id);
	gpu_set_tile_offset(0, 0);

	gpu_set_opaque(true);
	gpu_draw_tiles(vcur - vbuf - vcount, vcount);
	gpu_set_opaque(false);

	return vcount;
}

void video_hard_render(int drawn_fb, int previous_transfer_count) {
	gpu_target_screen(drawn_fb);

	int start_eye = eye_count == 2 ? 0 : tVBOpt.DEFAULT_EYE;
	int end_eye = start_eye + eye_count;

	for (int i = 0; i < AFFINE_CACHE_SIZE; i++) {
		tileMapCache[i].used = false;
		tileMapCache[i].lumin = tileMapCache[i].umin;
		tileMapCache[i].lumax = tileMapCache[i].umax;
		tileMapCache[i].lvmin = tileMapCache[i].vmin;
		tileMapCache[i].lvmax = tileMapCache[i].vmax;
	}

	gpu_clear_screen(start_eye, end_eye);

	gpu_setup_drawing();

	vcur = vbuf;
	avcur = avbuf;

	gpu_setup_tile_drawing();

	u16 *windows = (u16 *)(vb_state->V810_DISPLAY_RAM.off + 0x3d800);

	uint8_t object_group_id = 3;

	for (int8_t wnd = 31; wnd >= 0; wnd--) {
		if (windows[wnd * 16] & 0x40)
			break;
		if (!(windows[wnd * 16] & 0xc000))
			continue;
		int vcount = 0;

		if ((windows[wnd * 16] & 0x3000) != 0x3000) {
			// background world
			uint8_t mapid = windows[wnd * 16] & 0xf;
			uint8_t scx_pow = ((windows[wnd * 16] >> 10) & 3);
			uint8_t scy_pow = ((windows[wnd * 16] >> 8) & 3);
			uint8_t map_count_pow = scx_pow + scy_pow;
			bool huge_bg = map_count_pow > 3;
			if (huge_bg) map_count_pow = 3;
			uint8_t scx = 1 << scx_pow;
			uint8_t scy = 1 << scy_pow;
			uint8_t map_count = 1 << map_count_pow;
			mapid &= ~(map_count - 1);
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

			u16 *tilemap = (u16 *)(vb_state->V810_DISPLAY_RAM.off + 0x20000);

			if (h == 0) continue;

			if ((windows[wnd * 16] & 0x3000) == 0) {
				// normal world
				vertex *vstart = vcur;
				vcount = 0;

				int left_mx = base_mx - abs(mp);
				int right_mx = base_mx + abs(mp);
				int left_gx = base_gx - abs(gp);
				int right_gx = base_gx + abs(gp);

				if (right_gx + w <= 0 || left_gx >= 384) continue;
				if (gy + h <= 0 || gy >= 224) continue;

				int tsx = left_mx >> 3;
				int ty = my >> 3;
				int mapsx = tsx >> 6;
				int mapy = ty >> 6;
				tsx &= 63;
				ty &= 63;
				if (!over) {
					mapsx &= scx - 1;
					mapy &= scy - 1;
				}
				bool over_visible = !over || tileVisible[tilemap[over_tile] & 0x07ff];

				for (int y = gy - (my & 7); y < gy + h; y += 8) {
					if (y >= 224) break;
					if (y > -8 && (over_visible || (mapy & (scy - 1)) == mapy)) {
						int tx = tsx;
						int mapx = mapsx;
						int current_map = mapid + scx * mapy + mapx;
						for (int x = 0; x < 512; x += 8) {
							if (x + left_gx - abs(mp) * 2 - (left_mx & 7) >= 384) break;
							bool use_over = over && ((mapx & (scx - 1)) != mapx || (mapy & (scy - 1)) != mapy);
							uint16_t tile = tilemap[use_over ? over_tile : (64 * 64) * current_map + 64 * ty + tx];
							if (++tx >= 64) {
								tx = 0;
								if ((++mapx & (scx - 1)) == 0 && !over) mapx = 0;
								current_map = mapid + scx * mapy + mapx;
							}
							uint16_t tileid = tile & 0x07ff;
							if (!tileVisible[tileid]) continue;
							short u = (tileid % 32);
							short v = (tileid / 32);

							if (vcur >= vbuf + VBUF_SIZE) {
								dprintf(0, "VBUF OVERRUN!\n");
								break;
							}

							vcur->x = x;
							vcur->y = y;
							vcur->u = u;
							vcur->v = v;
							vcur->palette = tile >> 14;
							vcur++->orient = (tile >> 12) & 3;

							vcount++;
						}
					}
					if (++ty >= 64) {
						ty = 0;
						if (++mapy >= scy && !over) mapy = 0;
					}
				}

				if (vcount != 0) {
					for (int eye = start_eye; eye < end_eye; eye++) {
						if (!(windows[wnd * 16] & (0x8000 >> eye)))
							continue;
						int gx = base_gx;
						int mx = base_mx;
						if (eye == 0) {
							gx -= gp;
							mx -= mp;
						} else {
							gx += gp;
							mx += mp;
						}

						if (gx + w <= 0 || gx >= 384) continue;

						int offset_x = gx - mx + (left_mx & ~7);

						gpu_set_tile_offset(offset_x / 256.0, eye);

						// note: transposed
						gpu_set_scissor(true, (gy >= 0 ? gy : 0) + 256 * eye, gx >= 0 ? gx : 0, (gy + h < 256 ? gy + h : 256) + 256 * eye, gx + w);

						gpu_draw_tiles(vstart - vbuf, vcount);

						gpu_set_tile_offset(0, 0);
					}
				}
			} else {
				// hbias or affine world
				u16 param_base = windows[wnd * 16 + 9];
				s16 *params = (s16 *)(vb_state->V810_DISPLAY_RAM.off + 0x20000 + param_base * 2);

				int full_w = 512 * scx;
				int full_h = 512 * scy;

				// initial bounds calculation setup
				int umin, vmin, umax, vmax;
				if ((windows[wnd * 16] & 0x3000) == 0x1000) {
					// h-bias
					vmin = my;
					vmax = my + h;
					umin = umax = params[0];
				} else {
					// affine
					umin = umax = params[0] >> 3;
					vmin = vmax = params[2] >> 3;
				}

				// set up vertex buffers
				int vbuf_count = 0;
				avertex *vbufs[2] = {NULL, NULL};
				for (int eye = start_eye; eye < end_eye; eye++) {
					if (!(windows[wnd * 16] & (0x8000 >> eye)))
						continue;

					vbufs[eye] = avcur;

					int gx = base_gx;
					int mx = base_mx;

					if (eye == 0) {
						gx -= gp;
						mx -= mp;
					} else {
						gx += gp;
						mx += mp;
					}
					if ((windows[wnd * 16] & 0x3000) == 0x1000) {
						// hbias

						if (avcur + h >= avbuf + AVBUF_SIZE) {
							dprintf(0, "AVBUF OVERRUN!\n");
							break;
						}

						// Account for hardware flaw that uses OR rather than adding
						// when computing the address of HOFSTR.
						u8 eye_offset = eye && !(param_base & 1);
						for (int y = 0; y < h; y++) {
							s16 p = (s16)(params[y * 2 + eye_offset] << 3) >> 3;
							avcur->x1 = gx;
							avcur->y1 = gy + y + 256 * eye;
							avcur->x2 = gx + w;
							avcur->y2 = gy + y + 1 + 256 * eye;
							// we can do just one pass per bg for repeating multimap
							// because hbias isn't downscaled
							int u = mx + p;
							int v = my + y;
							if (u < umin) umin = u;
							if (u + w > umax) umax = u + w;
							avcur->u1 = u * 8;
							avcur->v1 = v * 8;
							avcur->u2 = (u + w) * 8;
							avcur->v2 = v * 8;
							avcur++;
						}
					} else {
						// affine
						if (avcur + h >= avbuf + AVBUF_SIZE) {
							dprintf(0, "AVBUF OVERRUN!\n");
							break;
						}
						for (int y = 0; y < h; y++) {
							s16 mx = params[y * 8 + 0];
							s16 mp = params[y * 8 + 1];
							s16 my = params[y * 8 + 2];
							s32 dx = params[y * 8 + 3];
							s32 dy = params[y * 8 + 4];
							avcur->x1 = gx;
							avcur->y1 = gy + y + 256 * eye;
							avcur->x2 = gx + w;
							avcur->y2 = gy + y + 256 * eye;
							avcur->u1 = avcur->u2 = mx + ((eye == 0) != (mp >= 0) ? abs(mp) * dx >> 6 : 0);
							avcur->v1 = avcur->v2 = my + ((eye == 0) != (mp >= 0) ? abs(mp) * dy >> 6 : 0);
							avcur->uoff1 = avcur->voff1 = 0;
							avcur->uoff2 = (dx * w >> 6);
							avcur->voff2 = (dy * w >> 6);
							if (umin > (avcur->u1 >> 3))
								umin = (avcur->u1 >> 3);
							if (umin > ((avcur->u2 + avcur->uoff2) >> 3))
								umin = ((avcur->u2 + avcur->uoff2) >> 3);
							if (umax < (avcur->u1 >> 3))
								umax = (avcur->u1 >> 3);
							if (umax < ((avcur->u2 + avcur->uoff2) >> 3))
								umax = ((avcur->u2 + avcur->uoff2) >> 3);
							if (vmin > (avcur->v1 >> 3))
								vmin = (avcur->v1 >> 3);
							if (vmin > ((avcur->v2 + avcur->voff2) >> 3))
								vmin = ((avcur->v2 + avcur->voff2) >> 3);
							if (vmax < (avcur->v1 >> 3))
								vmax = (avcur->v1 >> 3);
							if (vmax < ((avcur->v2 + avcur->voff2) >> 3))
								vmax = ((avcur->v2 + avcur->voff2) >> 3);
							avcur++;
						}
					}
				}
				if (avcur - avbuf > AVBUF_SIZE) dprintf(0, "AVBUF OVERRUN - %i/%i\n", avcur - avbuf, AVBUF_SIZE);

				if (vbufs[0] == NULL && vbufs[1] == NULL) continue;

				bool visible[8] = {0};
				for (int sub_bg = 0; sub_bg < map_count; sub_bg++) {
					int sub_u = (sub_bg & (scx - 1)) * 512;
					int sub_v = (sub_bg >> scx_pow) * 512;
					// don't draw offscreen bgmaps
					// with big backgrounds this is too complicated
					if (!huge_bg && (over || map_count == 1)) {
						if (!over) {
							// repeating
							// TODO can this be done faster with maths?
							while (sub_u + 512 > umin) sub_u -= scx * 512;
							while (sub_u + 512 <= umin) sub_u += scx * 512;
							if (sub_u > umax) {
								continue;
							}
							while (sub_v + 512 > vmin) sub_v -= scy * 512;
							while (sub_v + 512 <= vmin) sub_v += scy * 512;
							if (sub_v > vmax) {
								continue;
							}
						} else {
							// not repeating
							if (sub_u > umax || sub_u + 512 < umin || sub_v > vmax || sub_v + 512 < vmin) {
								continue;
							}
						}
					}
					visible[sub_bg] = true;
					vcur += render_affine_cache(mapid + sub_bg, vbuf, vcur, umin, umax, vmin, vmax);
				}

				gpu_target_screen(drawn_fb);
				gpu_draw_affine((WORLD*)&windows[wnd * 16], umin, vmin, umax, vmax, drawn_fb, vbufs, visible);
			}
			gpu_setup_tile_drawing();
		} else {
			// object world
			gpu_set_scissor(false, 0, 0, 0, 0);
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

				short u = (tileid % 32);
				short v = (tileid / 32);

				short palette = (cw3 >> 14) | 4;

				s16 jp = (s16)(cw1 << 6) >> 6;

				for (int eye = start_eye; eye < end_eye; eye++) {
					if (!(cw1 & (0x8000 >> eye)))
						continue;
					u16 x = base_x;
					if (eye == 0)
						x -= jp;
					else
						x += jp;

					if (vcur >= vbuf + VBUF_SIZE) {
						dprintf(0, "VBUF OVERRUN!\n");
						break;
					}

					vcur->x = x;
					vcur->y = y + 256 * eye;
					vcur->u = u;
					vcur->v = v;
					vcur->palette = palette;
					vcur++->orient = (cw3 >> 12) & 3;
					vcount++;
				}
			}
			object_group_id = (object_group_id - 1) & 3;
			
			if (vcur - vbuf > VBUF_SIZE) dprintf(0, "VBUF OVERRUN - %i/%i\n", vcur - vbuf, VBUF_SIZE);
			gpu_draw_tiles(vcur - vbuf - vcount, vcount);
		}
	}

	if (tVBOpt.RENDERMODE == RM_TOCPU) {
		gpu_init_vip_download(previous_transfer_count, start_eye, end_eye, drawn_fb);
	}

	// invalidate any unused bgmaps
	for (int i = 0; i < AFFINE_CACHE_SIZE; i++) {
		if (!tileMapCache[i].used) tileMapCache[i].bg = -1;
		memcpy(tileMapCache[i].GPLT, vb_state->tVIPREG.GPLT, sizeof(vb_state->tVIPREG.GPLT));
	}
}
