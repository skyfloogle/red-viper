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
}

// my stuff

u8 eye = 0;

C3D_RenderTarget *finalScreen[2];
C3D_Tex screenTex[2];
C3D_RenderTarget *screenTarget[2];

C3D_Tex tileTexture;
bool tileVisible[2048];

#define AFFINE_CACHE_SIZE 3
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

typedef struct
{
	short x1, y1, x2, y2;
	short u, v, palette;
} vertex;

typedef struct
{
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

bool V810_DSP_Init()
{
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
	params.height = 256;
	params.format = GPU_RGBA4;
	params.onVram = true;
	for (int i = 0; i < 2; i++)
	{
		C3D_TexInitWithParams(&screenTex[i], NULL, params);
		screenTarget[i] = C3D_RenderTargetCreateFromTex(&screenTex[i], GPU_TEX_2D, 0, GPU_RB_DEPTH16);
	}

	params.width = 512;
	params.height = 512;
	for (int i = 0; i < AFFINE_CACHE_SIZE; i++)
	{
		C3D_TexInitWithParams(&tileMapCache[i], NULL, params);
		tileMapCacheTarget[i] = C3D_RenderTargetCreateFromTex(&tileMapCache[i], GPU_TEX_2D, 0, GPU_RB_DEPTH16);
		C3D_RenderTargetClear(tileMapCacheTarget[i], C3D_CLEAR_ALL, 0, 0);
	}

	C3D_TexSetFilter(&tileTexture, GPU_NEAREST, GPU_NEAREST);

	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
	C3D_AlphaTest(true, GPU_GREATER, 0);

	vbuf = linearAlloc(sizeof(vertex) * VBUF_SIZE);
	avbuf = linearAlloc(sizeof(avertex) * AVBUF_SIZE);

	return true;
}

void setRegularTexEnv()
{
	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_DOT3_RGB);
	C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
}

void setRegularDrawing()
{
	C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_SHORT, 4);
	AttrInfo_AddLoader(attrInfo, 1, GPU_SHORT, 3);

	setRegularTexEnv();

	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc_posscale, 1.0 / (512 / 2), 1.0 / (256 / 2), -1.0, 1.0);
	memcpy(C3D_FVUnifWritePtr(GPU_VERTEX_SHADER, uLoc_palettes, 8), palettes, sizeof(palettes));
}

void sceneRender()
{
	float cols[4] = {0,
		((tVIPREG.BRTA & 0xff) + 0x80) / 256.0,
		((tVIPREG.BRTB & 0xff) + 0x80) / 256.0,
		(((tVIPREG.BRTA & 0xff) + (tVIPREG.BRTB & 0xff) + (tVIPREG.BRTC & 0xff)) + 0x80) / 256.0};
	u32 clearcol = (cols[tVIPREG.BKCOL] - 0.5) * 510;
	C3D_RenderTargetClear(screenTarget[eye], C3D_CLEAR_ALL, clearcol | (clearcol << 8) | (clearcol << 16) | 0xff000000, 0);
	for (int i = 0; i < 4; i++)
	{
		HWORD pal = tVIPREG.GPLT[i];
		palettes[i].x = cols[(pal >> 6) & 3];
		palettes[i].y = cols[(pal >> 4) & 3];
		palettes[i].z = cols[(pal >> 2) & 3];
		pal = tVIPREG.JPLT[i];
		palettes[i + 4].x = cols[(pal >> 6) & 3];
		palettes[i + 4].y = cols[(pal >> 4) & 3];
		palettes[i + 4].z = cols[(pal >> 2) & 3];
	}

	C3D_FrameDrawOn(screenTarget[eye]);
	setRegularDrawing();

	C3D_SetViewport(0, 0, 512, 256);
	C3D_TexBind(0, &tileTexture);
	C3D_BindProgram(&sChar);

	C3D_BufInfo *bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, vbuf, sizeof(vertex), 2, 0x10);

	C3D_CullFace(GPU_CULL_NONE);

	u16 *windows = (u16 *)(V810_DISPLAY_RAM.pmemory + 0x3d800);

	uint8_t object_group_id = 3;
	int cache_id = 0;

	for (int8_t wnd = 31; wnd >= 0; wnd--)
	{
		if (windows[wnd * 16] & 64)
			break;
		if (!(windows[wnd * 16] & 0xc000))
			continue;
		int vcount = 0;
		if ((windows[wnd * 16] & 0x3000) != 0x3000)
		{
			// background world
			if (!(windows[wnd * 16] & (0x8000 >> eye)))
				continue;
			uint8_t mapid = windows[wnd * 16] & 0xf;
			uint8_t scx = 1 << ((windows[wnd * 16] >> 10) & 3);
			uint8_t scy = 1 << ((windows[wnd * 16] >> 8) & 3);
			int16_t gx = windows[wnd * 16 + 1];
			int16_t gp = windows[wnd * 16 + 2];
			int16_t gy = windows[wnd * 16 + 3];
			int16_t mx = windows[wnd * 16 + 4] & 0xfff;
			if (mx & 0x800) mx |= 0xf000; 
			int16_t mp = windows[wnd * 16 + 5];
			int16_t my = windows[wnd * 16 + 6] & 0xfff;
			if (my & 0x800) my |= 0xf000;
			int16_t w = windows[wnd * 16 + 7] + 1;
			int16_t h = windows[wnd * 16 + 8] + 1;

			if (h == 0) continue;

			if (eye == 0)
			{
				gx -= gp;
				mx -= mp;
			}
			else
			{
				gx += gp;
				mx += mp;
			}

			if ((windows[wnd * 16] & 0x3000) == 0)
			{
				C3D_SetScissor(GPU_SCISSOR_NORMAL, gx >= 0 ? gx : 0, gy >= 0 ? gy : 0, gx + w, gy + h);
				// normal world
				u16 *tilemap = (u16 *)(V810_DISPLAY_RAM.pmemory + 0x20000);
				int tsx = mx >> 3;
				int ty = my >> 3;
				int mapx = tsx >> 6;
				int mapy = ty >> 6;
				tsx &= 63;
				ty &= 63;
				mapx %= scx;
				mapy %= scy;
				if (mapx < 0) mapx = (mapx + scx) % scx;
				if (mapy < 0) mapy = (mapy + scy) % scy;

				for (int y = gy - (my & 7); y < gy + h; y += 8)
				{
					int tx = tsx;
					int current_map = mapid + scx * mapy + mapx;
					for (int x = gx - (mx & 7); x < gx + w; x += 8)
					{
						uint16_t tile = tilemap[(64 * 64) * current_map + 64 * ty + tx];
						if (++tx >= 64) {
							tx = 0;
							if (++mapx % scx == 0) mapx = 0;
							current_map = mapid + scx * mapy + mapx;
						}
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
					if (++ty >= 64) {
						ty = 0;
						if (++mapy >= scy) mapy = 0;
					}
				}
			}
			else
			{
				// hbias or affine world
				int cache_y1, cache_y2;
				if ((windows[wnd * 16] & 0x3000) == 0x1000)
				{
					cache_y1 = my & ~7;
					cache_y2 = my + h;
				}
				else
				{
					cache_y1 = 0;
					cache_y2 = 64 * 8;
				}
				// first, render a cache
				// set up cache vertices

				u16 *tilemap = (u16 *)(V810_DISPLAY_RAM.pmemory + 0x20000 + 8192 * mapid) + 64 * (cache_y1 >> 3);
				for (int y = cache_y1; y < cache_y2; y += 8)
				{
					for (int x = 0; x < 64 * 8; x += 8)
					{
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
				C3D_SetViewport(0, 0, 512, 512);
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

				// next, draw the affine map
				C3D_FrameDrawOn(screenTarget[eye]);
				C3D_BindProgram(&sAffine);
				C3D_TexBind(0, &tileMapCache[cache_id]);
				C3D_SetViewport(0, 0, 512, 256);
				C3D_SetScissor(GPU_SCISSOR_NORMAL, gx >= 0 ? gx : 0, gy >= 0 ? gy : 0, gx + w, gy + h);

				C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
				AttrInfo_Init(attrInfo);
				AttrInfo_AddLoader(attrInfo, 0, GPU_SHORT, 4);
				AttrInfo_AddLoader(attrInfo, 1, GPU_SHORT, 2);
				AttrInfo_AddLoader(attrInfo, 2, GPU_SHORT, 4);

				bufInfo = C3D_GetBufInfo();
				BufInfo_Init(bufInfo);
				BufInfo_Add(bufInfo, avbuf, sizeof(avertex), 3, 0x210);

				env = C3D_GetTexEnv(0);
				C3D_TexEnvInit(env);
				C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
				C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

				s16 *params = (s16 *)(&V810_DISPLAY_RAM.pmemory[0x20000 + windows[wnd * 16 + 9] * 2]);

				if ((windows[wnd * 16] & 0x3000) == 0x1000)
				{
					// hbias
					for (int y = 0; y < h; y++)
					{
						s16 p = params[y * 2 + eye];
						if (p & 0x1000)
							p |= (s16)0xe000;
						avcur->x1 = gx;
						avcur->y1 = gy + y;
						avcur->x2 = gx + w;
						avcur->y2 = gy + y + 1;
						avcur->u = (mx + p) * 8;
						avcur->v = (my + y) * 8;
						avcur->ix = w * 8;
						avcur->iy = 0;
						avcur->jx = 0;
						avcur++->jy = 1 * 8;
					}
				}
				else
				{
					// affine
					for (int y = 0; y < h; y++)
					{
						mx = params[y * 8 + 0];
						mp = params[y * 8 + 1];
						my = params[y * 8 + 2];
						s32 dx = params[y * 8 + 3];
						s32 dy = params[y * 8 + 4];
						avcur->x1 = gx;
						avcur->y1 = gy + y;
						avcur->x2 = gx + w;
						avcur->y2 = gy + y + 1;
						avcur->u = mx + ((eye == 0) != (mp >= 0) ? abs(mp) * dx >> 6 : 0);
						avcur->v = my + ((eye == 0) != (mp >= 0) ? abs(mp) * dy >> 6 : 0);
						avcur->ix = dx * (w + mp) >> 6;
						avcur->iy = dy * (w + mp) >> 6;
						avcur->jx = params[y * 8 + 3] != 0 ? 0 : 1 * 8;
						avcur++->jy = params[y * 8 + 3] == 0 ? 0 : 1 * 8;
					}
				}
				if (avcur - avbuf > AVBUF_SIZE) printf("AVBUF OVERRUN - %i/%i\n", avcur - avbuf, AVBUF_SIZE);
				C3D_DrawArrays(GPU_GEOMETRY_PRIM, avcur - avbuf - h, h);

				bufInfo = C3D_GetBufInfo();
				BufInfo_Init(bufInfo);
				BufInfo_Add(bufInfo, vbuf, sizeof(vertex), 2, 0x10);
				C3D_BindProgram(&sChar);
				C3D_TexBind(0, &tileTexture);
				setRegularDrawing();
				vcount = 0;
				if (++cache_id == AFFINE_CACHE_SIZE)
				{
					cache_id = 0;
				}
			}
		}
		else
		{
			// object world
			C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
			int start_index = object_group_id == 0 ? 1023 : (tVIPREG.SPT[object_group_id - 1]) & 1023;
			int end_index = tVIPREG.SPT[object_group_id];
			for (int i = end_index; i != start_index; i = (i - 1) & 1023)
			{
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
				bool hflip = (cw3 & 0x2000) != 0;
				bool vflip = (cw3 & 0x1000) != 0;
				short u = (tileid % 32) * 8;
				short v = (tileid / 32) * 8;

				vcur->x1 = x + 8 * hflip;
				vcur->y1 = y + 8 * vflip;
				vcur->x2 = x + 8 * !hflip;
				vcur->y2 = y + 8 * !vflip;
				vcur->u = u;
				vcur->v = v;
				vcur++->palette = (cw3 >> 14) | 4;
				vcount++;
			}
			object_group_id = (object_group_id - 1) & 3;
		}
		if (vcur - vbuf > VBUF_SIZE) printf("VBUF OVERRUN - %i/%i\n", vcur - vbuf, VBUF_SIZE);
		if (vcount != 0)
			C3D_DrawArrays(GPU_GEOMETRY_PRIM, vcur - vbuf - vcount, vcount);
	}
}

void doAllTheDrawing()
{
	if (tDSPCACHE.CharCacheInvalid) {
		tDSPCACHE.CharCacheInvalid = false;
		uint16_t *texImage = C3D_Tex2DGetImagePtr(&tileTexture, 0, NULL);
		for (int t = 0; t < 2048; t++)
		{
			if (tDSPCACHE.CharacterCache[t])
				tDSPCACHE.CharacterCache[t] = false;
			else
				continue;
			uint16_t *tile = (uint16_t*)(V810_DISPLAY_RAM.pmemory + ((t & 0x600) << 6) + 0x6000 + (t & 0x1ff) * 16);
			// optimize invisible tiles
			tileVisible[t] = false;
			for (int i = 0; i < 2; i++) {
				if (((uint64_t *)(tile))[i]) {
					tileVisible[t] = true;
					break;
				}
			}
			int y = 63 - t / 32;
			int x = t % 32;
			uint16_t *dstbuf = texImage + ((y * 32 + x) * 8 * 8);
			for (int i = 0; i < 8 * 8; i++)
			{
				uint8_t sx = (i & 1) | ((i >> 1) & 2) | ((i >> 2) & 4);
				uint8_t sy = 7 - (((i >> 1) & 1) | ((i >> 2) & 2) | ((i >> 3) & 4));
				uint16_t row = tile[sy];
				while (sx-- > 0)
					row >>= 2;
				row &= 3;
				const static uint16_t colors[4] = {0, 0x88ff, 0x8f8f, 0xf88f};
				dstbuf[i] = colors[row];
			}
		}
	}

	C3D_FrameBegin(0);

	vcur = vbuf;
	avcur = avbuf;

	for (eye = 0; eye < 2; eye++)
	{
		sceneRender();

		C3D_RenderTargetClear(finalScreen[eye], C3D_CLEAR_ALL, 0, 0);
		C3D_FrameDrawOn(finalScreen[eye]);
		C3D_SetViewport((240 - 224) / 2, (400 - 384) / 2, 224, 384);
		C3D_TexBind(0, &screenTex[eye]);
		C3D_BindProgram(&sFinal);
		C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);

		C3D_TexEnv *env = C3D_GetTexEnv(0);
		C3D_TexEnvInit(env);
		C3D_TexEnvColor(env, 0xff0000ff);
		C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_CONSTANT, 0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);

		C3D_ImmDrawBegin(GPU_GEOMETRY_PRIM);
		C3D_ImmSendAttrib(1, 1, -1, 1);
		C3D_ImmSendAttrib(0, 0, 0, 0);
		C3D_ImmSendAttrib(-1, -1, -1, 1);
		C3D_ImmSendAttrib(384.0 / 512, 224.0 / 256, 0, 0);
		C3D_ImmDrawEnd();

		// 2D mode
		if (tVBOpt.DSPMODE == DM_NORMAL) break;
	}

	C3D_FrameEnd(0);
}

void V810_DSP_Quit() {
	C3D_Fini();
}
