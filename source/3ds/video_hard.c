#include <stdlib.h>
#include <string.h>
#include <3ds.h>
#include <citro3d.h>

#include "vb_dsp.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_set.h"

#include "n3ds_shaders.h"

#include <tex3ds.h>
#include "palette_mask_t3x.h"
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

C3D_Tex screenTexHard[2];
C3D_RenderTarget *screenTargetHard[2];

static C3D_Tex tileTexture;

// Virtual Bowling needs at least 4 for good performance
#define AFFINE_CACHE_SIZE 8
typedef struct {
	C3D_Tex tex;
	C3D_RenderTarget *target;
	int bg;
	short umin, umax, vmin, vmax;
	short lumin, lumax, lvmin, lvmax;
	u16 tiles[64 * 64];
	u8 GPLT[4];
	bool visible;
	bool used;
} AffineCacheEntry;
AffineCacheEntry tileMapCache[AFFINE_CACHE_SIZE];

static C3D_Tex affine_masks[4][4];
static C3D_Tex palette_mask;

static C3D_FVec pal1tex[8] = {0}, pal2tex[8] = {0}, pal3col[8] = {0};
static C3D_FVec char_offset;
static C3D_FVec bgmap_offsets[2];

typedef struct {
	short x, y;
	u8 u, v, palette, orient;
} __attribute__((aligned(4))) vertex;

typedef struct {
	short x1, y1, x2, y2;
	short u, v;
	short ix, iy, jx, jy;
} avertex;

#define VBUF_SIZE 64 * 64 * 2 * 32
static vertex *vbuf, *vcur;
#define AVBUF_SIZE 4096 * 8
static avertex *avbuf, *avcur;

void video_hard_init(void) {
	C3D_TexInitParams params;
	params.width = 256;
	params.height = 512;
	params.format = GPU_RGBA4;
	params.type = GPU_TEX_2D;
	params.onVram = false;
	params.maxLevel = 0;
	C3D_TexInitWithParams(&tileTexture, NULL, params);

	params.width = 512;
	params.height = 512;
	params.format = GPU_RGBA4;
	params.onVram = true;
	for (int i = 0; i < 2; i++) {
		C3D_TexInitWithParams(&screenTexHard[i], NULL, params);
		// Drawing backwards with a depth buffer isn't faster, so omit the depth buffer.
		screenTargetHard[i] = C3D_RenderTargetCreateFromTex(&screenTexHard[i], GPU_TEXFACE_2D, 0, -1);
		C3D_RenderTargetClear(screenTargetHard[i], C3D_CLEAR_ALL, 0, 0);
	}

	params.width = 512;
	params.height = 512;
	params.format = GPU_RGBA4;
	for (int i = 0; i < AFFINE_CACHE_SIZE; i++) {
		C3D_TexInitWithParams(&tileMapCache[i].tex, NULL, params);
		tileMapCache[i].target = C3D_RenderTargetCreateFromTex(&tileMapCache[i].tex, GPU_TEXFACE_2D, 0, -1);
		C3D_RenderTargetClear(tileMapCache[i].target, C3D_CLEAR_ALL, 0xffffffff, 0);
		tileMapCache[i].bg = -1;
	}

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

	Tex3DS_TextureFree(Tex3DS_TextureImport(palette_mask_t3x, palette_mask_t3x_size, &palette_mask, NULL, false));

	C3D_TexSetFilter(&tileTexture, GPU_NEAREST, GPU_NEAREST);

	C3D_ColorLogicOp(GPU_LOGICOP_COPY);

	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);

	vbuf = linearAlloc(sizeof(vertex) * VBUF_SIZE);
	avbuf = linearAlloc(sizeof(avertex) * AVBUF_SIZE);
}

static void setRegularTexEnv(void) {
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_TEXTURE1, 0);
	C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_R, 0, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);

	env = C3D_GetTexEnv(1);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_TEXTURE2, GPU_PREVIOUS);
	C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_G, 0, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);

	env = C3D_GetTexEnv(2);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PREVIOUS);
	C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_B, 0, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);

	C3D_TexEnvInit(C3D_GetTexEnv(3));
}

static void setRegularDrawing(void) {
	C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_SHORT, 2);
	AttrInfo_AddLoader(attrInfo, 1, GPU_BYTE, 4);

	setRegularTexEnv();

	C3D_TexBind(1, &palette_mask);
	C3D_TexBind(2, &palette_mask);

	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc.posscale, 1.0 / (512 / 2), 1.0 / (512 / 2), -1.0, 1.0);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc.offset, 0, 0, 0, 0);
	memcpy(C3D_FVUnifWritePtr(GPU_GEOMETRY_SHADER, uLoc.pal1tex, 8), pal1tex, sizeof(pal1tex));
	memcpy(C3D_FVUnifWritePtr(GPU_GEOMETRY_SHADER, uLoc.pal2tex, 8), pal2tex, sizeof(pal2tex));
	memcpy(C3D_FVUnifWritePtr(GPU_GEOMETRY_SHADER, uLoc.pal3col, 8), pal3col, sizeof(pal3col));
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
	C3D_FrameDrawOn(cache->target);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc.posscale, 1.0 / (512 / 2), 1.0 / (512 / 2), -1.0, 1.0);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc.offset, 0, 0, 0, 0);
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);

	C3D_AlphaTest(false, GPU_GREATER, 0);
	if (vcount != 0) C3D_DrawArrays(GPU_GEOMETRY_PRIM, vcur - vbuf - vcount, vcount);
	C3D_AlphaTest(true, GPU_GREATER, 0);

	return vcount;
}

static void draw_affine_layer(int drawn_fb, avertex *vbufs[], C3D_Tex **textures, int count, int base_gx, int gp, int gy, int w, int h, bool use_masks) {
	C3D_FrameDrawOn(screenTargetHard[drawn_fb]);
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
			
			C3D_SetScissor(GPU_SCISSOR_NORMAL, gx >= 0 ? gx : 0, 256 * eye + (gy >= 0 ? gy : 0), gx + w, (gy + h < 256 ? gy + h : 256) + 256 * eye);
			C3D_DrawArrays(GPU_GEOMETRY_PRIM, vbufs[eye] - avbuf, h);
		}
	}

	bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, vbuf, sizeof(vertex), 2, 0x10);
	C3D_BindProgram(&sChar);
	C3D_TexBind(0, &tileTexture);
	setRegularDrawing();
}

void video_hard_render(int drawn_fb) {
	C3D_FrameDrawOn(screenTargetHard[drawn_fb]);

	int start_eye = eye_count == 2 ? 0 : tVBOpt.DEFAULT_EYE;
	int end_eye = start_eye + eye_count;

	for (int i = 0; i < AFFINE_CACHE_SIZE; i++) {
		tileMapCache[i].used = false;
		tileMapCache[i].lumin = tileMapCache[i].umin;
		tileMapCache[i].lumax = tileMapCache[i].umax;
		tileMapCache[i].lvmin = tileMapCache[i].vmin;
		tileMapCache[i].lvmax = tileMapCache[i].vmax;
	}

	// clear
	C3D_BindProgram(&sFinal);
	C3D_AlphaTest(false, GPU_GREATER, 0);

	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	// black, red, green, blue
	const static u32 colors[4] = {0, 0xff0000ff, 0xff00ff00, 0xffff0000};
	C3D_TexEnvColor(env, colors[vb_state->tVIPREG.BKCOL]);
	C3D_TexEnvSrc(env, C3D_Both, GPU_CONSTANT, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

	C3D_TexEnvInit(C3D_GetTexEnv(1));
	C3D_TexEnvInit(C3D_GetTexEnv(2));

	C3D_ImmDrawBegin(GPU_GEOMETRY_PRIM);
	// left
	if (start_eye == 0) {
		C3D_ImmSendAttrib(384.0/256-1, 224.0/256-1, -1, -1);
		C3D_ImmSendAttrib(0, 0, 0, 0);
		C3D_ImmSendAttrib(0, 0, 0, 0);
	}
	// right
	if (end_eye == 2) {
		C3D_ImmSendAttrib(384.0/256-1, 224.0/256, -1, 0);
		C3D_ImmSendAttrib(0, 0, 0, 0);
		C3D_ImmSendAttrib(0, 0, 0, 0);
	}
	C3D_ImmDrawEnd();
	C3D_AlphaTest(true, GPU_GREATER, 0);

	for (int i = 0; i < 4; i++) {
		const C3D_FVec cols[4] = {{}, {.x = 1}, {.y = 1}, {.z = 1}};
		HWORD pal = vb_state->tVIPREG.GPLT[i];
		pal1tex[i].x = (((pal >> 1) & 0b110) + 1) / 8.0;
		pal2tex[i].x = (((pal >> 3) & 0b110) + 1) / 8.0;
		memcpy(&pal3col[i], &cols[(pal >> 6) & 3], sizeof(C3D_FVec));
		pal = vb_state->tVIPREG.JPLT[i];
		pal1tex[i + 4].x = (((pal >> 1) & 0b110) + 1) / 8.0;
		pal2tex[i + 4].x = (((pal >> 3) & 0b110) + 1) / 8.0;
		memcpy(&pal3col[i + 4], &cols[(pal >> 6) & 3], sizeof(C3D_FVec));
	}

	vcur = vbuf;
	avcur = avbuf;

	setRegularDrawing();

	C3D_TexBind(0, &tileTexture);
	C3D_BindProgram(&sChar);

	C3D_BufInfo *bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, vbuf, sizeof(vertex), 2, 0x10);

	C3D_CullFace(GPU_CULL_NONE);

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

						C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc.offset,
							offset_x / 256.0,
							eye, 0, 0);

						C3D_SetScissor(GPU_SCISSOR_NORMAL, gx >= 0 ? gx : 0, (gy >= 0 ? gy : 0) + 256 * eye, gx + w, (gy + h < 256 ? gy + h : 256) + 256 * eye);

						C3D_DrawArrays(GPU_GEOMETRY_PRIM, vstart - vbuf, vcount);

						C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc.offset, 0, 0, 0, 0);
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

				int map_count = scx * scy;
				bool huge_bg = map_count > 8;
				if (huge_bg) map_count = 8;
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
					int sub_u = (sub_bg & (scx - 1)) * 512;
					int sub_v = (sub_bg >> scx_pow) * 512;
					// don't draw offscreen bgmaps
					// with masks on this is too complicated
					if (!use_masks) {
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
					} else if (sub_bg % 2 == 0) {
						// new draw, adjust mask offsets
						bgmap_offsets[1].x = sub_bg & (scx - 1);
						bgmap_offsets[1].y = sub_bg >> scx_pow;
					}

					vcur += render_affine_cache(mapid + sub_bg, vbuf, vcur, umin, umax, vmin, vmax);
					int cache_id = (mapid + sub_bg) % AFFINE_CACHE_SIZE;
					if (tileMapCache[cache_id].bg != mapid + sub_bg || !tileMapCache[cache_id].visible) continue;

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
			}
		} else {
			// object world
			C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
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
			if (vcount != 0) C3D_DrawArrays(GPU_GEOMETRY_PRIM, vcur - vbuf - vcount, vcount);
		}
	}

	// invalidate any unused bgmaps
	for (int i = 0; i < AFFINE_CACHE_SIZE; i++) {
		if (!tileMapCache[i].used) tileMapCache[i].bg = -1;
		memcpy(tileMapCache[i].GPLT, vb_state->tVIPREG.GPLT, sizeof(vb_state->tVIPREG.GPLT));
	}
}

void update_texture_cache_hard(void) {
	uint16_t *texImage = C3D_Tex2DGetImagePtr(&tileTexture, 0, NULL);
	blankTile = -1;
	for (int t = 0; t < 2048; t++) {
		// skip if this tile wasn't modified
		if (!tDSPCACHE.CharacterCache[t]) {
			if (blankTile < 0 && !tileVisible[t])
				blankTile = t;
			continue;
		}

		uint32_t *tile = (uint32_t*)(vb_state->V810_DISPLAY_RAM.off + ((t & 0x600) << 6) + 0x6000 + (t & 0x1ff) * 16);

		int y = 63 - t / 32;
		int x = t % 32;
		uint32_t *dstbuf = (uint32_t*)(texImage + ((y * 32 + x) * 8 * 8));

		// optimize invisible tiles
		{
			bool tv = ((uint64_t*)tile)[0] | ((uint64_t*)tile)[1];
			tileVisible[t] = tv;
			if (!tv) {
				if (blankTile < 0) {
					blankTile = t;
				}
				memset(dstbuf, 0, 8 * 8 * 2);
				continue;
			}
		}
		
		for (int i = 2; i >= 0; i -= 2) {
			uint32_t slice1 = tile[i + 1];
			uint32_t slice2 = tile[i];
	
			// black, red, green, blue
			const static uint16_t colors[4] = {0, 0xf00f, 0x0f0f, 0x00ff};

			#define SQUARE(x, i) { \
				uint32_t left  = x >> (0 + 4*i) & 0x00030003; \
				uint32_t right = x >> (2 + 4*i) & 0x00030003; \
				*dstbuf++ = colors[left >> 16] | (colors[right >> 16] << 16); \
				*dstbuf++ = colors[(uint16_t)left] | (colors[(uint16_t)right] << 16); \
			}

			SQUARE(slice1, 0);
			SQUARE(slice1, 1);
			SQUARE(slice2, 0);
			SQUARE(slice2, 1);
			SQUARE(slice1, 2);
			SQUARE(slice1, 3);
			SQUARE(slice2, 2);
			SQUARE(slice2, 3);

			#undef SQUARE
		}
	}
}
