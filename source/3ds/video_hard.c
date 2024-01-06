#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>
#include <citro3d.h>

#include "vb_dsp.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_set.h"

#include "char_shbin.h"
#include "final_shbin.h"
#include "affine_shbin.h"

C3D_Tex screenTexHard;
C3D_RenderTarget *screenTarget;

C3D_Tex tileTexture;

#define AFFINE_CACHE_SIZE 4
C3D_Tex tileMapCache[AFFINE_CACHE_SIZE];
C3D_RenderTarget *tileMapCacheTarget[AFFINE_CACHE_SIZE];

DVLB_s *char_dvlb;
shaderProgram_s sChar;
s8 uLoc_posscale;
s8 uLoc_palettes;
C3D_FVec palettes[8];

DVLB_s *sAffine_dvlb;
shaderProgram_s sAffine;

typedef struct {
	short x1, y1, x2, y2;
	short u, v, palette;
} vertex;

typedef struct {
	short x1, y1, x2, y2;
	short u, v;
	short ix, iy, jx, jy;
} avertex;

#define VBUF_SIZE 64 * 64 * 2 * 32
vertex *vbuf, *vcur;
#define AVBUF_SIZE 4096
avertex *avbuf, *avcur;

void video_hard_init() {
	char_dvlb = DVLB_ParseFile((u32 *)char_shbin, char_shbin_size);
	shaderProgramInit(&sChar);
	shaderProgramSetVsh(&sChar, &char_dvlb->DVLE[0]);
	shaderProgramSetGsh(&sChar, &char_dvlb->DVLE[1], 4);

	uLoc_posscale = shaderInstanceGetUniformLocation(sChar.vertexShader, "posscale");
	uLoc_palettes = shaderInstanceGetUniformLocation(sChar.vertexShader, "palettes");

	sAffine_dvlb = DVLB_ParseFile((u32 *)affine_shbin, affine_shbin_size);
	shaderProgramInit(&sAffine);
	shaderProgramSetVsh(&sAffine, &sAffine_dvlb->DVLE[0]);
	shaderProgramSetGsh(&sAffine, &sAffine_dvlb->DVLE[1], 3);

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
	C3D_TexInitWithParams(&screenTexHard, NULL, params);
	screenTarget = C3D_RenderTargetCreateFromTex(&screenTexHard, GPU_TEX_2D, 0, GPU_RB_DEPTH16);
	C3D_RenderTargetClear(screenTarget, C3D_CLEAR_ALL, 0, 0);

	params.width = 512;
	params.height = 512;
	for (int i = 0; i < AFFINE_CACHE_SIZE; i++) {
		C3D_TexInitWithParams(&tileMapCache[i], NULL, params);
		tileMapCacheTarget[i] = C3D_RenderTargetCreateFromTex(&tileMapCache[i], GPU_TEX_2D, 0, GPU_RB_DEPTH16);
	}

	C3D_TexSetFilter(&tileTexture, GPU_NEAREST, GPU_NEAREST);

	C3D_ColorLogicOp(GPU_LOGICOP_COPY);

	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
	C3D_AlphaTest(true, GPU_GREATER, 0);

	vbuf = linearAlloc(sizeof(vertex) * VBUF_SIZE);
	avbuf = linearAlloc(sizeof(avertex) * AVBUF_SIZE);
}

void setRegularTexEnv() {
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_DOT3_RGB);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
}

void setRegularDrawing() {
	C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_SHORT, 4);
	AttrInfo_AddLoader(attrInfo, 1, GPU_SHORT, 3);

	setRegularTexEnv();

	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc_posscale, 1.0 / (512 / 2), 1.0 / (512 / 2), -1.0, 1.0);
	memcpy(C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, uLoc_palettes, 8), palettes, sizeof(palettes));
}

void video_hard_render() {
	u8 clearcol = brightness[tVIPREG.BKCOL];
	C3D_RenderTargetClear(screenTarget, C3D_CLEAR_ALL, ((clearcol | (clearcol << 8) | (clearcol << 16)) << 2) | 0xff000000, 0);
	for (int i = 0; i < 4; i++) {
		HWORD pal = tVIPREG.GPLT[i];
		palettes[i].x = brightness[(pal >> 6) & 3] / 256.0 + 0.5;
		palettes[i].y = brightness[(pal >> 4) & 3] / 256.0 + 0.5; 
		palettes[i].z = brightness[(pal >> 2) & 3] / 256.0 + 0.5;
		pal = tVIPREG.JPLT[i];
		palettes[i + 4].x = brightness[(pal >> 6) & 3] / 256.0 + 0.5;
		palettes[i + 4].y = brightness[(pal >> 4) & 3] / 256.0 + 0.5;
		palettes[i + 4].z = brightness[(pal >> 2) & 3] / 256.0 + 0.5;
	}

	vcur = vbuf;
	avcur = avbuf;

	C3D_FrameDrawOn(screenTarget);
	setRegularDrawing();

	C3D_TexBind(0, &tileTexture);
	C3D_BindProgram(&sChar);

	C3D_BufInfo *bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, vbuf, sizeof(vertex), 2, 0x10);

	C3D_CullFace(GPU_CULL_NONE);

	u16 *windows = (u16 *)(V810_DISPLAY_RAM.pmemory + 0x3d800);

	uint8_t object_group_id = 3;

	int cached_backgrounds[AFFINE_CACHE_SIZE];
	int cache_visible[AFFINE_CACHE_SIZE];
	for (int i = 0; i < AFFINE_CACHE_SIZE; i++) cached_backgrounds[i] = -1;

	for (int8_t wnd = 31; wnd >= 0; wnd--) {
		if (windows[wnd * 16] & 0x40)
			break;
		if (!(windows[wnd * 16] & 0xc000))
			continue;
		int vcount = 0;
		
		#define DRAW_VBUF \
			if (vcur - vbuf > VBUF_SIZE) printf("VBUF OVERRUN - %i/%i\n", vcur - vbuf, VBUF_SIZE); \
			if (vcount != 0) C3D_DrawArrays(GPU_GEOMETRY_PRIM, vcur - vbuf - vcount, vcount);

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
			int16_t mp = (s16)(windows[wnd * 16 + 5] << 1) >> 3;
			int16_t my = (s16)(windows[wnd * 16 + 6] << 3) >> 3;
			int16_t w = windows[wnd * 16 + 7] + 1;
			int16_t h = windows[wnd * 16 + 8] + 1;
			int16_t over_tile = windows[wnd * 16 + 10] & 0x7ff;

			if (h == 0) continue;

			if ((windows[wnd * 16] & 0x3000) == 0) {
				// normal world
				for (int eye = 0; eye < eye_count; eye++) {
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
					vcount = 0;

					C3D_SetScissor(GPU_SCISSOR_NORMAL, gx >= 0 ? gx : 0, (gy >= 0 ? gy : 0) + 256 * eye, gx + w, (gy + h < 256 ? gy + h : 256) + 256 * eye);
					u16 *tilemap = (u16 *)(V810_DISPLAY_RAM.pmemory + 0x20000);
					int tsx = mx >> 3;
					int ty = my >> 3;
					int mapsx = tsx >> 6;
					int mapy = ty >> 6;
					tsx &= 63;
					ty &= 63;
					if (!over) {
						mapsx &= scx - 1;
						mapy &= scy - 1;
					}
					bool over_visible = !over || tileVisible[over_tile];

					for (int y = gy - (my & 7); y < gy + h; y += 8) {
						if (over_visible || (mapy & (scy - 1)) == mapy) {
							int tx = tsx;
							int mapx = mapsx;
							int current_map = mapid + scx * mapy + mapx;
							for (int x = gx - (mx & 7); x < gx + w; x += 8) {
								bool use_over = over && ((mapx & (scx - 1)) != mapx || (mapy & (scy - 1)) != mapy);
								uint16_t tile = tilemap[use_over ? over_tile : (64 * 64) * current_map + 64 * ty + tx];
								if (++tx >= 64) {
									tx = 0;
									if ((++mapx & (scx - 1)) == 0 && !over) mapx = 0;
									current_map = mapid + scx * mapy + mapx;
								}
								uint16_t tileid = tile & 0x07ff;
								if (!tileVisible[tileid]) continue;
								bool hflip = (tile & 0x2000) != 0;
								bool vflip = (tile & 0x1000) != 0;
								short u = (tileid % 32) * 8;
								short v = (tileid / 32) * 8;

								vcur->x1 = x + 8 * hflip;
								vcur->y1 = y + 8 * vflip + 256 * eye;
								vcur->x2 = x + 8 * !hflip;
								vcur->y2 = y + 8 * !vflip + 256 * eye;
								vcur->u = u;
								vcur->v = v;
								vcur++->palette = tile >> 14;

								vcount++;
							}
						}
						if (++ty >= 64) {
							ty = 0;
							if (++mapy >= scy && !over) mapy = 0;
						}
					}
					DRAW_VBUF;
				}
			} else {
				// hbias or affine world
				for (uint8_t sub_bg = 0; sub_bg < scx * scy; sub_bg++) {
					int cache_id = (mapid + sub_bg) % AFFINE_CACHE_SIZE;
					if (cached_backgrounds[cache_id] != mapid + sub_bg) {
						cached_backgrounds[cache_id] = mapid + sub_bg;
						int cache_y1, cache_y2;
						if ((windows[wnd * 16] & 0x3000) == 0x1000) {
							// with this caching thing, scanning only part of it isn't really viable
							/*
							cache_y1 = (sub_bg & (scy - 1)) == 0 ? my & ~7 : 0;
							cache_y2 = (sub_bg & (scy - 1)) == scy - 1 ?  my + h : 64 * 8;
							*/
							cache_y1 = 0;
							cache_y2 = 64 * 8;
						} else {
							cache_y1 = 0;
							cache_y2 = 64 * 8;
						}
						// first, render a cache
						// set up cache vertices

						u16 *tilemap = (u16 *)(V810_DISPLAY_RAM.pmemory + 0x20000 + 8192 * (mapid + sub_bg)) + 64 * (cache_y1 >> 3);
						for (int y = cache_y1; y < cache_y2; y += 8) {
							for (int x = 0; x < 64 * 8; x += 8) {
								uint16_t tile = *tilemap++;
								uint16_t tileid = tile & 0x07ff;
								if (!tileVisible[tileid]) continue;
								bool hflip = (tile & 0x2000) != 0;
								bool vflip = (tile & 0x1000) != 0;
								short u = (tileid % 32) * 8;
								short v = (tileid / 32) * 8;

								vcur->x1 = x + 8 * hflip;
								vcur->y1 = y + 8 * vflip;
								vcur->x2 = x + 8 * !hflip;
								vcur->y2 = y + 8 * !vflip;
								vcur->u = u;
								vcur->v = v;
								vcur++->palette = tile >> 14;

								vcount++;
							}
						}
						if (vcount == 0) {
							// bail
							cache_visible[cache_id] = false;
							continue;
						}
						cache_visible[cache_id] = true;
						if (vcur - vbuf > VBUF_SIZE) printf("VBUF OVERRUN - %i/%i\n", vcur - vbuf, VBUF_SIZE);

						// set up cache texture
						C3D_FrameDrawOn(tileMapCacheTarget[cache_id]);
						C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc_posscale, 1.0 / (512 / 2), 1.0 / (512 / 2), -1.0, 1.0);
						C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);

						// clear
						C3D_BindProgram(&sFinal);
						C3D_ColorLogicOp(GPU_LOGICOP_CLEAR);
						C3D_AlphaTest(false, GPU_GREATER, 0);

						C3D_TexEnv *env = C3D_GetTexEnv(0);
						C3D_TexEnvInit(env);
						C3D_TexEnvColor(env, 0);
						C3D_TexEnvSrc(env, C3D_Both, GPU_CONSTANT, 0, 0);
						C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

						C3D_ImmDrawBegin(GPU_GEOMETRY_PRIM);
						C3D_ImmSendAttrib(1, 1, -1, 1);
						C3D_ImmSendAttrib(0, 0, 0, 0);
						C3D_ImmSendAttrib(-1, -1, -1, 1);
						C3D_ImmSendAttrib(1, 1, 0, 0);
						C3D_ImmDrawEnd();

						// reset and draw cache
						C3D_BindProgram(&sChar);
						C3D_ColorLogicOp(GPU_LOGICOP_COPY);
						C3D_AlphaTest(true, GPU_GREATER, 0);
						setRegularTexEnv();
						
						C3D_DrawArrays(GPU_GEOMETRY_PRIM, vcur - vbuf - vcount, vcount);
					} else if (!cache_visible[cache_id]) continue;

					// set up wrapping for affine map
					C3D_TexSetWrap(&tileMapCache[cache_id],
						!over && scx == 1 ? GPU_REPEAT : GPU_CLAMP_TO_BORDER,
						!over && scy == 1 ? GPU_REPEAT : GPU_CLAMP_TO_BORDER);
					if (over && tileVisible[over_tile]) {
						static bool warned = false;
						if (!warned) {
							warned = true;
							if ((windows[wnd * 16] & 0x3000) == 0x1000)
								puts("WARN:Can't do overplane in H-Bias yet");
							else
								puts("WARN:Can't do overplane in Affine yet");
						}
					}

					// next, draw the affine map
					C3D_FrameDrawOn(screenTarget);
					C3D_BindProgram(&sAffine);
					C3D_TexBind(0, &tileMapCache[cache_id]);

					C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
					AttrInfo_Init(attrInfo);
					AttrInfo_AddLoader(attrInfo, 0, GPU_SHORT, 4);
					AttrInfo_AddLoader(attrInfo, 1, GPU_SHORT, 2);
					AttrInfo_AddLoader(attrInfo, 2, GPU_SHORT, 4);

					bufInfo = C3D_GetBufInfo();
					BufInfo_Init(bufInfo);
					BufInfo_Add(bufInfo, avbuf, sizeof(avertex), 3, 0x210);

					C3D_TexEnv *env = C3D_GetTexEnv(0);
					C3D_TexEnvInit(env);
					C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
					C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

					s16 *params = (s16 *)(&V810_DISPLAY_RAM.pmemory[0x20000 + windows[wnd * 16 + 9] * 2]);

					int full_w = 512 * scx;
					int full_h = 512 * scy;

					for (int eye = 0; eye < eye_count; eye++) {
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
						
						C3D_SetScissor(GPU_SCISSOR_NORMAL, gx >= 0 ? gx : 0, 256 * eye + (gy >= 0 ? gy : 0), gx + w, (gy + h < 256 ? gy + h : 256) + 256 * eye);
						
						int base_u = -512 * (sub_bg & (scx - 1));
						int base_v = -512 * (sub_bg >> scx_pow);

						if ((windows[wnd * 16] & 0x3000) == 0x1000) {
							// hbias
							base_u += mx;
							base_v += my;
							for (int y = 0; y < h; y++) {
								s16 p = (s16)(params[y * 2 + eye] << 3) >> 3;
								avcur->x1 = gx;
								avcur->y1 = gy + y + 256 * eye;
								avcur->x2 = gx + w;
								avcur->y2 = gy + y + 1 + 256 * eye;
								// we can do just one pass per bg for repeating multimap
								// because hbias isn't downscaled
								int u = base_u + p;
								int v = base_v + y;
								if (!over) {
									u &= full_w - 1;
									if (u & (full_w >> 1)) u -= full_w;
									v &= full_h - 1;
									if (v & (full_h >> 1)) v -= full_h;
								}
								avcur->u = u * 8;
								avcur->v = v * 8;
								avcur->ix = w * 8;
								avcur->iy = 0;
								avcur->jx = 0;
								avcur++->jy = 1 * 8;
							}
						} else {
							// affine
							// TODO handle repeating multimap
							for (int y = 0; y < h; y++) {
								mx = params[y * 8 + 0];
								mp = params[y * 8 + 1];
								my = params[y * 8 + 2];
								s32 dx = params[y * 8 + 3];
								s32 dy = params[y * 8 + 4];
								avcur->x1 = gx;
								avcur->y1 = gy + y + 256 * eye;
								avcur->x2 = gx + w;
								avcur->y2 = gy + y + 1 + 256 * eye;
								avcur->u = base_u * 8 + mx + ((eye == 0) != (mp >= 0) ? abs(mp) * dx >> 6 : 0);
								avcur->v = base_v * 8 + my + ((eye == 0) != (mp >= 0) ? abs(mp) * dy >> 6 : 0);
								avcur->ix = dx * (w + mp) >> 6;
								avcur->iy = dy * (w + mp) >> 6;
								avcur->jx = params[y * 8 + 3] != 0 ? 0 : 1 * 8;
								avcur++->jy = params[y * 8 + 3] == 0 ? 0 : 1 * 8;
							}
						}
						if (avcur - avbuf > AVBUF_SIZE) printf("AVBUF OVERRUN - %i/%i\n", avcur - avbuf, AVBUF_SIZE);
						C3D_DrawArrays(GPU_GEOMETRY_PRIM, avcur - avbuf - h, h);
					}

					bufInfo = C3D_GetBufInfo();
					BufInfo_Init(bufInfo);
					BufInfo_Add(bufInfo, vbuf, sizeof(vertex), 2, 0x10);
					C3D_BindProgram(&sChar);
					C3D_TexBind(0, &tileTexture);
					setRegularDrawing();
					vcount = 0;
				}
			}
		} else {
			// object world
			C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
			int start_index = object_group_id == 0 ? 1023 : (tVIPREG.SPT[object_group_id - 1]) & 1023;
			int end_index = tVIPREG.SPT[object_group_id] & 1023;
			for (int i = end_index; i != start_index; i = (i - 1) & 1023) {
				u16 *obj_ptr = (u16 *)(&V810_DISPLAY_RAM.pmemory[0x0003E000 + 8 * i]);

				u16 cw3 = obj_ptr[3];
				u16 tileid = cw3 & 0x07ff;
				if (!tileVisible[tileid]) continue;

				u16 x = obj_ptr[0];
				u16 cw1 = obj_ptr[1];
				s16 y = *(u8*)&obj_ptr[2];
				if (y > 224) y = (s8)y;

				bool hflip = (cw3 & 0x2000) != 0;
				bool vflip = (cw3 & 0x1000) != 0;
				short u = (tileid % 32) * 8;
				short v = (tileid / 32) * 8;

				short palette = (cw3 >> 14) | 4;

				s16 jp = (s16)(cw1 << 6) >> 6;

				for (int eye = 0; eye < eye_count; eye++) {
					if (!(cw1 & (0x8000 >> eye)))
						continue;
					if (eye == 0)
						x -= jp;
					else
						x += jp;

					vcur->x1 = x + 8 * hflip;
					vcur->y1 = y + 8 * vflip + 256 * eye;
					vcur->x2 = x + 8 * !hflip;
					vcur->y2 = y + 8 * !vflip + 256 * eye;
					vcur->u = u;
					vcur->v = v;
					vcur++->palette = palette;
					vcount++;
				}
			}
			object_group_id = (object_group_id - 1) & 3;
			
			DRAW_VBUF;
		}
	}
}

void update_texture_cache_hard() {
	uint16_t *texImage = C3D_Tex2DGetImagePtr(&tileTexture, 0, NULL);
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

		int y = 63 - t / 32;
		int x = t % 32;
		uint32_t *dstbuf = (uint32_t*)(texImage + ((y * 32 + x) * 8 * 8));
		
		for (int i = 2; i >= 0; i -= 2) {
			uint32_t slice1 = tile[i + 1];
			uint32_t slice2 = tile[i];
	
			const static uint16_t colors[4] = {0, 0x88ff, 0x8f8f, 0xf88f};

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
