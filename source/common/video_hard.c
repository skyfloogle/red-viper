#include <stdlib.h>
#include <string.h>

#include "vb_dsp.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_set.h"
#include "video_hard.h"

#ifdef __3DS__
#include "../3ds/n3ds_shaders.h"
#include <tex3ds.h>
#include "map1x1_t3x.h"
#include "map1x2_t3x.h"
#include "map1x4_t3x.h"
#include "map1x8_t3x.h"
#include "map2x1_t3x.h"
#include "map2x2_t3x.h"
#include "map2x4_t3x.h"
#include "map4x1_t3x.h"
#include "map4x2_t3x.h"
#include "map4x4_t3x.h"
#include "map8x1_t3x.h"
#include "map8x2_t3x.h"
#include "map8x4_t3x.h"

static C3D_Tex affine_masks[4][4];
static C3D_FVec bgmap_offsets[2];
#endif

AffineCacheEntry tileMapCache[AFFINE_CACHE_SIZE];

vertex *vbuf, *vcur;
avertex *avbuf, *avcur;

uint16_t *rgba4_framebuffers;


void video_hard_init(void) {
	gpu_init();

	#ifdef __3DS__
	#define LOAD_MASK(w,h,wp,hp) \
		Tex3DS_TextureFree(Tex3DS_TextureImport(map ## w ## x ## h ## _t3x, map ## w ## x ## h ## _t3x_size, &affine_masks[wp][hp], NULL, false));
	LOAD_MASK(1, 1, 0, 0);
	LOAD_MASK(1, 2, 0, 1);
	LOAD_MASK(1, 4, 0, 2);
	LOAD_MASK(1, 8, 0, 3);
	LOAD_MASK(2, 1, 1, 0);
	LOAD_MASK(2, 2, 1, 1);
	LOAD_MASK(2, 4, 1, 2);
	LOAD_MASK(4, 1, 2, 0);
	LOAD_MASK(4, 2, 2, 1);
	LOAD_MASK(4, 4, 2, 2);
	LOAD_MASK(8, 1, 3, 0);
	LOAD_MASK(8, 2, 3, 1);
	LOAD_MASK(8, 4, 3, 2);
	#undef LOAD_MASK
	affine_masks[3][3] = affine_masks[2][3] = affine_masks[1][3] = affine_masks[0][3];
	#endif

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
	gpu_set_target(cache->target);
	gpu_set_tile_offset(0, 0);
	gpu_set_scissor(false, 0, 0, 0, 0);

	gpu_set_opaque(true);
	gpu_draw_tiles(vcur - vbuf - vcount, vcount);
	gpu_set_opaque(false);

	return vcount;
}

#ifdef __3DS__
static void draw_affine_layer(int drawn_fb, avertex *vbufs[], C3D_Tex **textures, int count, int base_gx, int gp, int gy, int w, int h, bool use_masks) {
	gpu_set_target(screenTargetHard[drawn_fb]);
	C3D_BindProgram(&sAffine);

	if (!use_masks) {
		for (int i = 0; i < count; i++) {
			C3D_TexBind(i, textures[i]);
			C3D_TexEnv *env = C3D_GetTexEnv(i);
			C3D_TexEnvInit(env);
			C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0 + i, GPU_PREVIOUS, 0);
			C3D_TexEnvFunc(env, C3D_Both, i == 0 ? GPU_REPLACE : GPU_ADD);
		}
		for (int i = count; i < 4; i++) {
			C3D_TexEnvInit(C3D_GetTexEnv(i));
		}
	} else {
		for (int i = 0; i < count; i++) {
			C3D_TexBind(i, textures[i]);
			C3D_TexEnv *env = C3D_GetTexEnv(i);
			C3D_TexEnvInit(env);
			C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0 + i, GPU_TEXTURE2, GPU_PREVIOUS);
			C3D_TexEnvOpRgb(env, 0, i == 0 ? GPU_TEVOP_RGB_SRC_R : GPU_TEVOP_RGB_SRC_G, 0);
			C3D_TexEnvOpAlpha(env, 0, i == 0 ? GPU_TEVOP_A_SRC_R : GPU_TEVOP_A_SRC_G, 0);
			C3D_TexEnvFunc(env, C3D_Both, i == 0 ? GPU_MODULATE : GPU_MULTIPLY_ADD);
		}
		for (int i = count; i < 4; i++) {
			C3D_TexEnvInit(C3D_GetTexEnv(i));
		}
	}

	memcpy(C3D_FVUnifWritePtr(GPU_GEOMETRY_SHADER, uLoc.bgmap_offsets, 2), bgmap_offsets, sizeof(bgmap_offsets));

	C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_SHORT, 4);
	AttrInfo_AddLoader(attrInfo, 1, GPU_SHORT, 2);
	AttrInfo_AddLoader(attrInfo, 2, GPU_SHORT, 4);

	C3D_BufInfo *bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, avbuf, sizeof(avertex), 3, 0x210);

	for (int eye = 0; eye < 2; eye++) {
		if (vbufs[eye] != NULL) {
			int gx = base_gx + (eye == 0 ? -gp : gp);
			
			// note: transposed
			gpu_set_scissor(true, 256 * eye + (gy >= 0 ? gy : 0), gx >= 0 ? gx : 0, (gy + h < 256 ? gy + h : 256) + 256 * eye, gx + w);
			C3D_DrawArrays(GPU_GEOMETRY_PRIM, vbufs[eye] - avbuf, h);
		}
	}

	gpu_setup_tile_drawing();
}
#endif

void video_hard_render(int drawn_fb, int previous_transfer_count) {
	gpu_set_target(screenTargetHard[drawn_fb]);

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
							avcur->u = u * 8;
							avcur->v = v * 8;
							avcur->ix = w * 8;
							avcur->iy = 0;
							avcur->jx = 0;
							avcur++->jy = 0;
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
							avcur->y2 = gy + y + 1 + 256 * eye;
							avcur->u = mx + ((eye == 0) != (mp >= 0) ? abs(mp) * dx >> 6 : 0);
							avcur->v = my + ((eye == 0) != (mp >= 0) ? abs(mp) * dy >> 6 : 0);
							avcur->ix = dx * w >> 6;
							avcur->iy = dy * w >> 6;
							if (umin > (avcur->u >> 3))
								umin = (avcur->u >> 3);
							if (umin > ((avcur->u + avcur->ix) >> 3))
								umin = ((avcur->u + avcur->ix) >> 3);
							if (umax < (avcur->u >> 3))
								umax = (avcur->u >> 3);
							if (umax < ((avcur->u + avcur->ix) >> 3))
								umax = ((avcur->u + avcur->ix) >> 3);
							if (vmin > (avcur->v >> 3))
								vmin = (avcur->v >> 3);
							if (vmin > ((avcur->v + avcur->iy) >> 3))
								vmin = ((avcur->v + avcur->iy) >> 3);
							if (vmax < (avcur->v >> 3))
								vmax = (avcur->v >> 3);
							if (vmax < ((avcur->v + avcur->iy) >> 3))
								vmax = ((avcur->v + avcur->iy) >> 3);
							avcur->jx = 0;
							avcur++->jy = 0;
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

				#ifdef __3DS__
				bool use_masks = huge_bg || (!over && map_count != 1);
				if (use_masks) {
					C3D_TexSetWrap(&affine_masks[scx_pow][scy_pow], over ? GPU_CLAMP_TO_BORDER : GPU_REPEAT, over ? GPU_CLAMP_TO_BORDER : GPU_REPEAT);
					bgmap_offsets[1].x = 0;
					bgmap_offsets[1].y = 0;
					bgmap_offsets[1].z = 1.0f / scx;
					bgmap_offsets[1].w = 1.0f / scy;
				} else {
					bgmap_offsets[1].x = 0;
					bgmap_offsets[1].y = 0;
					bgmap_offsets[1].z = 1;
					bgmap_offsets[1].w = 1;
				}

				// draw with each texture
				int tex_count = 0;
				C3D_Tex *textures[3];
				for (uint8_t sub_bg = 0; sub_bg < map_count; sub_bg++) {
					if (use_masks && sub_bg % 2 == 0) {
						// new draw, adjust mask offsets
						bgmap_offsets[1].x = sub_bg & (scx - 1);
						bgmap_offsets[1].y = sub_bg >> scx_pow;
					}

					if (!visible[sub_bg]) continue;

					int cache_id = (mapid + sub_bg) % AFFINE_CACHE_SIZE;
					if (tileMapCache[cache_id].bg != mapid + sub_bg || !tileMapCache[cache_id].visible) continue;

					int sub_u = (sub_bg & (scx - 1)) * 512;
					int sub_v = (sub_bg >> scx_pow) * 512;

					// set up wrapping for affine map
					C3D_TexSetWrap(&tileMapCache[cache_id].tex,
						use_masks || (!over && scx == 1) ? GPU_REPEAT : GPU_CLAMP_TO_BORDER,
						use_masks || (!over && scy == 1) ? GPU_REPEAT : GPU_CLAMP_TO_BORDER);
					if (over && tileVisible[tilemap[over_tile] & 0x07ff]) {
						static bool warned = false;
						if (!warned) {
							warned = true;
							if ((windows[wnd * 16] & 0x3000) == 0x1000)
								dprintf(0, "WARN:Can't do overplane in H-Bias yet\n");
							else
								dprintf(0, "WARN:Can't do overplane in Affine yet\n");
						}
					}

					if (use_masks) {
						C3D_TexBind(2, &affine_masks[scx_pow][scy_pow]);
					}

					sub_u &= full_w - 1;
					sub_v &= full_h - 1;
					int base_u_min = -sub_u, base_u_max = -sub_u - 1;
					int base_v_min = -sub_v, base_v_max = -sub_v - 1;
					if (!over && !use_masks) {
						// repeating maskless
						if (scx != 1) {
							base_u_min -= umin & ~(full_w - 1);
							base_u_max = -umax;
						}
						if (scy != 1) {
							base_v_min -= vmin & ~(full_h - 1);
							base_v_max = -vmax;
						}
					}
					int old_tex_count = tex_count;
					for (int base_u = base_u_min; base_u > base_u_max; base_u -= full_w) {
						for (int base_v = base_v_min; base_v > base_v_max; base_v -= full_h) {
							bgmap_offsets[tex_count / 2].c[3 - 2 * (tex_count % 2)] = base_u >> 9;
							bgmap_offsets[tex_count / 2].c[3 - 2 * (tex_count % 2) - 1] = base_v >> 9;
							textures[tex_count] = &tileMapCache[cache_id].tex;
							if (++tex_count == (use_masks ? 2 : 3)) {
								draw_affine_layer(drawn_fb, vbufs, textures, tex_count, base_gx, gp, gy, w, h, use_masks);
								tex_count = 0;
							}
						}
					}
				}
				// clean up any leftovers
				if (tex_count != 0) {
					draw_affine_layer(drawn_fb, vbufs, textures, tex_count, base_gx, gp, gy, w, h, use_masks);
				}
				#else
				(void)visible; // silence compiler warning
				gpu_set_target(screenTargetHard[drawn_fb]);
				#endif
			}
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
