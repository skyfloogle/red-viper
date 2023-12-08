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

// stuff copied from vb_dsp.c

VB_DSPCACHE tDSPCACHE; // Array of Display Cache info...
// Keybd Fn's. Had to put it somewhere!

extern int arm_keys;
// Read the Controller, Fix Me....
HWORD V810_RControll() {
    int ret_keys = 0;
    int key = 0;

#ifdef __3DS__
    key = hidKeysHeld();
#else
    ret_keys = arm_keys;
    arm_keys = 0;
#endif
    if (key & vbkey[14])        ret_keys |= VB_BATERY_LOW;  // Batery Low
    if (key & vbkey[13])        ret_keys |= VB_KEY_L;       // L Trigger
    if (key & vbkey[12])        ret_keys |= VB_KEY_R;       // R Trigger
    if (key & vbkey[11])        ret_keys |= VB_KEY_SELECT;  // Select Button
    if (key & vbkey[10])        ret_keys |= VB_KEY_START;   // Start Button
    if (key & vbkey[9])         ret_keys |= VB_KEY_B;       // B Button
    if (key & vbkey[8])         ret_keys |= VB_KEY_A;       // A Button
    if (key & vbkey[7])         ret_keys |= VB_RPAD_R;      // Right Pad, Right
    else if (key & vbkey[6])    ret_keys |= VB_RPAD_L;      // Right Pad, Left
    if (key & vbkey[5])         ret_keys |= VB_RPAD_D;      // Right Pad, Down
    else if (key & vbkey[4])    ret_keys |= VB_RPAD_U;      // Right Pad, Up
    if (key & vbkey[3])         ret_keys |= VB_LPAD_R;      // Left Pad, Right
    else if (key & vbkey[2])    ret_keys |= VB_LPAD_L;      // Left Pad, Left
    if (key & vbkey[1])         ret_keys |= VB_LPAD_D;      // Left Pad, Down
    else if (key & vbkey[0])    ret_keys |= VB_LPAD_U;      // Left Pad, Up

    //uint8_t battery_level;
    //PTMU_GetBatteryLevel(NULL, &battery_level);
    //if (battery_level <= 1)     ret_keys |= VB_BATERY_LOW;

    ret_keys = ret_keys|0x0002; // Always set bit1, ctrl ID
    return ret_keys;
}

void clearCache() {
    int i;
    tDSPCACHE.BgmPALMod = 1;                // World Palette Changed
    tDSPCACHE.ObjPALMod = 1;                // Obj Palette Changed
    tDSPCACHE.BrtPALMod = 1;                // Britness for Palette Changed
    tDSPCACHE.ObjDataCacheInvalid = 1;      // Object Cache Is invalid
    tDSPCACHE.ObjCacheInvalid = 1;          // Object Cache Is invalid
    for(i = 0; i < 14; i++)
        tDSPCACHE.BGCacheInvalid[i] = 1;    // Object Cache Is invalid
    tDSPCACHE.DDSPDataWrite = 1;            // Direct Screen Draw changed
	tDSPCACHE.CharCacheInvalid = true;
	for (i = 0; i < 2048; i++)
		tDSPCACHE.CharacterCache[i] = true;
	tDSPCACHE.ColumnTableInvalid = true;
}

// my stuff

C3D_RenderTarget *finalScreen[2];
C3D_Tex screenTex;
C3D_RenderTarget *screenTarget;

C3D_Tex tileTexture;
bool tileVisible[2048];

#define AFFINE_CACHE_SIZE 4
C3D_Tex tileMapCache[AFFINE_CACHE_SIZE];
C3D_RenderTarget *tileMapCacheTarget[AFFINE_CACHE_SIZE];

DVLB_s *char_dvlb;
shaderProgram_s sChar;
s8 uLoc_posscale;
s8 uLoc_palettes;
C3D_FVec palettes[8];

DVLB_s *sFinal_dvlb;
shaderProgram_s sFinal;

DVLB_s *sAffine_dvlb;
shaderProgram_s sAffine;

// We have two ways of dealing with the colours:
// 1. multiply base colours by max repeat, and scale down in postprocessing
//  -> lighter darks, less saturated lights
// 2. same but delay up to a factor of 4 until postprocessing using texenv scale
//  -> more saturated lights, barely (if at all) visible darks
// Method 2 would be more accurate if we had gamma correction,
// but I don't know how to do that.
// So for now, we'll use method 1, as it looks better IMO.
// To use method 2, uncomment the following line:
//#define COLTABLESCALE
static uint8_t maxRepeat = 0, minRepeat = 0;
C3D_Tex columnTableTexture[2];

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

#define DISPLAY_TRANSFER_FLAGS                                                                     \
	(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |               \
	 GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
	 GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

bool V810_DSP_Init() {
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4);
	finalScreen[0] = C3D_RenderTargetCreate(240, 400, GPU_RB_RGB8, GPU_RB_DEPTH16);
	C3D_RenderTargetSetOutput(finalScreen[0], GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
	finalScreen[1] = C3D_RenderTargetCreate(240, 400, GPU_RB_RGB8, GPU_RB_DEPTH16);
	C3D_RenderTargetSetOutput(finalScreen[1], GFX_TOP, GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);

	char_dvlb = DVLB_ParseFile((u32 *)char_shbin, char_shbin_size);
	shaderProgramInit(&sChar);
	shaderProgramSetVsh(&sChar, &char_dvlb->DVLE[0]);
	shaderProgramSetGsh(&sChar, &char_dvlb->DVLE[1], 4);

	uLoc_posscale = shaderInstanceGetUniformLocation(sChar.vertexShader, "posscale");
	uLoc_palettes = shaderInstanceGetUniformLocation(sChar.vertexShader, "palettes");

	sFinal_dvlb = DVLB_ParseFile((u32 *)final_shbin, final_shbin_size);
	shaderProgramInit(&sFinal);
	shaderProgramSetVsh(&sFinal, &sFinal_dvlb->DVLE[0]);
	shaderProgramSetGsh(&sFinal, &sFinal_dvlb->DVLE[1], 4);

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
	C3D_TexInitWithParams(&screenTex, NULL, params);
	screenTarget = C3D_RenderTargetCreateFromTex(&screenTex, GPU_TEX_2D, 0, GPU_RB_DEPTH16);

	params.width = 512;
	params.height = 512;
	for (int i = 0; i < AFFINE_CACHE_SIZE; i++) {
		C3D_TexInitWithParams(&tileMapCache[i], NULL, params);
		tileMapCacheTarget[i] = C3D_RenderTargetCreateFromTex(&tileMapCache[i], GPU_TEX_2D, 0, GPU_RB_DEPTH16);
		C3D_RenderTargetClear(tileMapCacheTarget[i], C3D_CLEAR_ALL, 0, 0);
	}

	params.width = 128;
	params.height = 8;
	params.format = GPU_L8;
	params.onVram = false;
	for (int i = 0; i < 2; i++) {
		C3D_TexInitWithParams(&columnTableTexture[i], NULL, params);
	}

	C3D_TexSetFilter(&tileTexture, GPU_NEAREST, GPU_NEAREST);

	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
	C3D_AlphaTest(true, GPU_GREATER, 0);

	vbuf = linearAlloc(sizeof(vertex) * VBUF_SIZE);
	avbuf = linearAlloc(sizeof(avertex) * AVBUF_SIZE);

	return true;
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

void processColumnTable() {
	u8 *table = V810_DISPLAY_RAM.pmemory + 0x3dc01;
	minRepeat = maxRepeat = table[0];
	for (int i = 1; i < 512; i++) {
		u8 r = table[i * 2];
		if (r < minRepeat) minRepeat = r;
		if (r > maxRepeat) maxRepeat = r;
	}
	// if maxRepeat would be 3 or 5+, make sure it's divisible by 4
	#ifdef COLTABLESCALE
	if (maxRepeat == 2 || maxRepeat > 3) {
		minRepeat += 4 - (maxRepeat & 3);
		maxRepeat += 4 - (maxRepeat & 3);
	} else
	#endif
	{
		minRepeat++;
		maxRepeat++;
	}
	if (minRepeat != maxRepeat) {
		// populate the bottom row of the textures
		for (int t = 0; t < 2; t++) {
			uint8_t *tex = C3D_Tex2DGetImagePtr(&columnTableTexture[t], 0, NULL);
			for (int i = 0; i < 96; i++) {
				tex[((i & ~0xf) << 3) | ((i & 8) << 3) | ((i & 4) << 2) | ((i & 2) << 1) | (i & 1)
					] = 255 * (1 + table[t * 512 + (255 - i) * 2]) / maxRepeat;
			}
		}
	}
}

void sceneRender() {
	#ifdef COLTABLESCALE
	int col_scale = maxRepeat >= 4 ? maxRepeat / 4 : 1;
	#else
	int col_scale = maxRepeat;
	#endif
	float cols[4] = {0,
		(tVIPREG.BRTA * col_scale + 0x80) / 256.0,
		(tVIPREG.BRTB * col_scale + 0x80) / 256.0,
		((tVIPREG.BRTA + tVIPREG.BRTB + tVIPREG.BRTC) * col_scale + 0x80) / 256.0};
	u32 clearcol = (cols[tVIPREG.BKCOL] - 0.5) * 510;
	C3D_RenderTargetClear(screenTarget, C3D_CLEAR_ALL, clearcol | (clearcol << 8) | (clearcol << 16) | 0xff000000, 0);
	for (int i = 0; i < 4; i++) {
		HWORD pal = tVIPREG.GPLT[i];
		palettes[i].x = cols[(pal >> 6) & 3];
		palettes[i].y = cols[(pal >> 4) & 3];
		palettes[i].z = cols[(pal >> 2) & 3];
		pal = tVIPREG.JPLT[i];
		palettes[i + 4].x = cols[(pal >> 6) & 3];
		palettes[i + 4].y = cols[(pal >> 4) & 3];
		palettes[i + 4].z = cols[(pal >> 2) & 3];
	}

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
			int16_t base_gx = windows[wnd * 16 + 1];
			int16_t gp = windows[wnd * 16 + 2];
			int16_t gy = windows[wnd * 16 + 3];
			int16_t base_mx = windows[wnd * 16 + 4] & 0xfff;
			if (base_mx & 0x800) base_mx |= 0xf000; 
			int16_t mp = windows[wnd * 16 + 5];
			int16_t my = windows[wnd * 16 + 6] & 0xfff;
			if (my & 0x800) my |= 0xf000;
			int16_t w = windows[wnd * 16 + 7] + 1;
			int16_t h = windows[wnd * 16 + 8] + 1;
			int16_t over_tile = windows[wnd * 16 + 10];

			if (h == 0) continue;

			if ((windows[wnd * 16] & 0x3000) == 0) {
				// normal world
				for (int eye = 0; eye < 2; eye++) {
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
					bool over_visible = !over || tileVisible[tilemap[over_tile] & 0x07ff];

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
							continue;
						}
						if (vcur - vbuf > VBUF_SIZE) printf("VBUF OVERRUN - %i/%i\n", vcur - vbuf, VBUF_SIZE);

						// set up cache texture
						C3D_FrameDrawOn(tileMapCacheTarget[cache_id]);
						C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc_posscale, 1.0 / (512 / 2), 1.0 / (512 / 2), -1.0, 1.0);
						C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);

						// clear
						C3D_BindProgram(&sFinal);
						C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
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
						C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
						C3D_AlphaTest(true, GPU_GREATER, 0);
						setRegularTexEnv();
						
						C3D_DrawArrays(GPU_GEOMETRY_PRIM, vcur - vbuf - vcount, vcount);
					}

					// set up wrapping for affine map
					C3D_TexSetWrap(&tileMapCache[cache_id],
						!over && scx == 1 ? GPU_REPEAT : GPU_CLAMP_TO_BORDER,
						!over && scy == 1 ? GPU_REPEAT : GPU_CLAMP_TO_BORDER);
					if (over && tileVisible[over_tile]) {
						if ((windows[wnd * 16] & 0x3000) == 0x1000)
							puts("WARN:Overplane for H-Bias not implemented");
						else
							puts("WARN:Overplane for Affine not implemented");
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

					for (int eye = 0; eye < 2; eye++) {
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
						
						int base_u = -512 * (sub_bg & (scy - 1));
						int base_v = -512 * (sub_bg >> scy_pow);

						if ((windows[wnd * 16] & 0x3000) == 0x1000) {
							// hbias
							base_u += mx;
							base_v += my;
							for (int y = 0; y < h; y++) {
								s16 p = params[y * 2 + eye];
								if (p & 0x1000)
									p |= (s16)0xe000;
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
								avcur->u = base_u + mx + ((eye == 0) != (mp >= 0) ? abs(mp) * dx >> 6 : 0);
								avcur->v = base_v + my + ((eye == 0) != (mp >= 0) ? abs(mp) * dy >> 6 : 0);
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
			for (int eye = 0; eye < 2; eye++) {
				C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
				int start_index = object_group_id == 0 ? 1023 : (tVIPREG.SPT[object_group_id - 1]) & 1023;
				int end_index = tVIPREG.SPT[object_group_id];
				for (int i = end_index; i != start_index; i = (i - 1) & 1023) {
					u16 *obj_ptr = (u16 *)(&V810_DISPLAY_RAM.pmemory[0x0003E000 + 8 * i]);
					u16 x = obj_ptr[0];
					u16 cw1 = obj_ptr[1];
					u16 y = obj_ptr[2];
					u16 cw3 = obj_ptr[3];

					if (!(cw1 & (0x8000 >> eye)))
						continue;

					s16 jp = cw1 & 0x1ff;
					if (jp & 0x100)
						jp |= 0xfe00;
					if (eye == 0)
						x -= jp;
					else
						x += jp;

					u16 tileid = cw3 & 0x07ff;
					if (!tileVisible[tileid]) continue;
					bool hflip = (cw3 & 0x2000) != 0;
					bool vflip = (cw3 & 0x1000) != 0;
					short u = (tileid % 32) * 8;
					short v = (tileid / 32) * 8;

					vcur->x1 = x + 8 * hflip;
					vcur->y1 = y + 8 * vflip + 256 * eye;
					vcur->x2 = x + 8 * !hflip;
					vcur->y2 = y + 8 * !vflip + 256 * eye;
					vcur->u = u;
					vcur->v = v;
					vcur++->palette = (cw3 >> 14) | 4;
					vcount++;
				}
			}
			object_group_id = (object_group_id - 1) & 3;
			
			DRAW_VBUF;
		}
	}
}

void doAllTheDrawing() {
	if (tDSPCACHE.CharCacheInvalid) {
		tDSPCACHE.CharCacheInvalid = false;
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
			uint16_t *dstbuf = texImage + ((y * 32 + x) * 8 * 8);
			
			for (int i = 2; i >= 0; i -= 2) {
				uint32_t slice1 = tile[i + 1];
				uint32_t slice2 = tile[i];
		
				const static uint16_t colors[4] = {0, 0x88ff, 0x8f8f, 0xf88f};

				#define SQUARE(x, i) { \
					uint32_t left  = x >> (0 + 4*i) & 0x00030003; \
					uint32_t right = x >> (2 + 4*i) & 0x00030003; \
					*dstbuf++ = colors[left  >> 16];              \
					*dstbuf++ = colors[right >> 16];              \
					*dstbuf++ = colors[(uint16_t)left];           \
					*dstbuf++ = colors[(uint16_t)right];          \
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

	C3D_FrameBegin(0);

	vcur = vbuf;
	avcur = avbuf;
	
	if (tDSPCACHE.ColumnTableInvalid)
		processColumnTable();

	sceneRender();
	
	C3D_TexBind(0, &screenTex);
	C3D_BindProgram(&sFinal);
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);

	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvColor(env, 0xff0000ff);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_CONSTANT, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
	if (minRepeat != maxRepeat) {
		env = C3D_GetTexEnv(0);
		env = C3D_GetTexEnv(1);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_TEXTURE1, 0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
		#ifdef COLTABLESCALE
		C3D_TexEnvScale(env, C3D_RGB, maxRepeat <= 2 ? maxRepeat - 1 : GPU_TEVSCALE_4);
		#endif
	}

	for (int eye = 0; eye < 2; eye++) {
		C3D_RenderTargetClear(finalScreen[eye], C3D_CLEAR_ALL, 0, 0);
		C3D_FrameDrawOn(finalScreen[eye]);
		C3D_SetViewport((240 - 224) / 2, (400 - 384) / 2, 224, 384);
		C3D_TexBind(1, &columnTableTexture[eye]);

		C3D_ImmDrawBegin(GPU_GEOMETRY_PRIM);
		C3D_ImmSendAttrib(1, 1, -1, 1);
		C3D_ImmSendAttrib(0, eye ? 0.5 : 0, 0, 0);
		C3D_ImmSendAttrib(-1, -1, -1, 1);
		C3D_ImmSendAttrib(384.0 / 512, (eye ? 0.5 : 0) + 224.0 / 512, 0, 0);
		C3D_ImmDrawEnd();
	}

	if (minRepeat != maxRepeat) {
		env = C3D_GetTexEnv(1);
		C3D_TexEnvInit(env);
	}

	C3D_FrameEnd(0);
}

void V810_DSP_Quit() {
	C3D_Fini();
}
