#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <3ds.h>
#include <citro3d.h>

#include "utils.h"

#include "vb_dsp.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_set.h"

#include "final_shbin.h"
#include "soft_shbin.h"

// some stuff copied from vb_dsp.c

VB_DSPCACHE tDSPCACHE; // Array of Display Cache info...
// Keybd Fn's. Had to put it somewhere!

extern int arm_keys;
// Read the Controller, Fix Me....
HWORD V810_RControll() {
    int ret_keys = 0;
    int key = 0;
	bool battery_low = false;

#ifdef __3DS__
    key = hidKeysHeld();
	// on citra checking the battery floods the logs
	if (!is_citra) {
		u8 charging, battery_level;
		PTMU_GetBatteryChargeState(&charging);
		PTMU_GetBatteryLevel(&battery_level);
		battery_low = !charging && battery_level <= 2;
	}

#else
    ret_keys = arm_keys;
    arm_keys = 0;
#endif
    if (battery_low) ret_keys |= VB_BATERY_LOW;
	for (int i = 0; i < 32; i++) {
		if (key & BIT(i)) ret_keys |= vbkey[i];
	}

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
    for (i = 0; i < 2; i++)
		tDSPCACHE.DDSPDataState[i] = CPU_CLEAR; // Direct Screen Draw changed
	tDSPCACHE.CharCacheInvalid = true;
	for (i = 0; i < 2048; i++)
		tDSPCACHE.CharacterCache[i] = true;
	tDSPCACHE.ColumnTableInvalid = true;
}

C3D_RenderTarget *finalScreen[2];

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
uint8_t maxRepeat = 0, minRepeat = 0;
C3D_Tex columnTableTexture[2];

int eye_count;

DVLB_s *sFinal_dvlb;
shaderProgram_s sFinal;

DVLB_s *sSoft_dvlb;
shaderProgram_s sSoft;

u8 brightness[4];

bool tileVisible[2048];
int blankTile;

void processColumnTable() {
	u8 *table = V810_DISPLAY_RAM.pmemory + 0x3dc01;
	uint8_t newMaxRepeat, newMinRepeat;
	newMinRepeat = newMaxRepeat = table[160];
	for (int t = 0; t < 2; t++) {
		for (int i = 161; i < 256; i++) {
			u8 r = table[t * 512 + i * 2];
			if (r < newMinRepeat) newMinRepeat = r;
			if (r > newMaxRepeat) newMaxRepeat = r;
		}
	}
	// if maxRepeat would be 3 or 5+, make sure it's divisible by 4
	#ifdef COLTABLESCALE
	if (newMaxRepeat == 2 || newMaxRepeat > 3) {
		minRepeat += 4 - (newMaxRepeat & 3);
		newMaxRepeat += 4 - (newMaxRepeat & 3);
	} else
	#endif
	{
		newMinRepeat++;
		newMaxRepeat++;
	}
	minRepeat = newMinRepeat;
	maxRepeat = newMaxRepeat;
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

void video_init() {
    #define DISPLAY_TRANSFER_FLAGS                                                                     \
        (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |               \
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4);
	finalScreen[0] = C3D_RenderTargetCreate(240, 400, GPU_RB_RGB8, -1);
	C3D_RenderTargetSetOutput(finalScreen[0], GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
	finalScreen[1] = C3D_RenderTargetCreate(240, 400, GPU_RB_RGB8, -1);
	C3D_RenderTargetSetOutput(finalScreen[1], GFX_TOP, GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);

	sFinal_dvlb = DVLB_ParseFile((u32 *)final_shbin, final_shbin_size);
	shaderProgramInit(&sFinal);
	shaderProgramSetVsh(&sFinal, &sFinal_dvlb->DVLE[0]);
	shaderProgramSetGsh(&sFinal, &sFinal_dvlb->DVLE[1], 4);

	sSoft_dvlb = DVLB_ParseFile((u32 *)soft_shbin, soft_shbin_size);
	shaderProgramInit(&sSoft);
	shaderProgramSetVsh(&sSoft, &sSoft_dvlb->DVLE[0]);
	shaderProgramSetGsh(&sSoft, &sSoft_dvlb->DVLE[1], 4);

	C3D_TexInitParams params;
	params.width = 128;
	params.height = 8;
	params.format = GPU_L8;
	params.type = GPU_TEX_2D;
	params.onVram = false;
	params.maxLevel = 0;
	for (int i = 0; i < 2; i++) {
		C3D_TexInitWithParams(&columnTableTexture[i], NULL, params);
	}

    video_hard_init();
	video_soft_init();
}

void video_render(int alt_buf) {
	C3D_FrameBegin(0);

	#ifdef COLTABLESCALE
	int col_scale = maxRepeat >= 4 ? maxRepeat / 4 : 1;
	#else
	int col_scale = maxRepeat;
	#endif
	brightness[0] = 0;
	brightness[1] = tVIPREG.BRTA * col_scale;
	if (brightness[1] > 127) brightness[1] = 127;
	brightness[2] = tVIPREG.BRTB * col_scale;
	if (brightness[2] > 127) brightness[2] = 127;
	brightness[3] = (tVIPREG.BRTA + tVIPREG.BRTB + tVIPREG.BRTC) * col_scale;
	if (brightness[3] > 127) brightness[3] = 127;

	C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_SHORT, 4);
	AttrInfo_AddLoader(attrInfo, 1, GPU_SHORT, 3);

	eye_count = CONFIG_3D_SLIDERSTATE > 0.0f ? 2 : 1;
		
	if (tDSPCACHE.ColumnTableInvalid)
		processColumnTable();

	if (tVIPREG.XPCTRL & 0x0002) {
		if (tDSPCACHE.CharCacheInvalid) {
			tDSPCACHE.CharCacheInvalid = false;
			if (tVBOpt.RENDERMODE < 2)
				update_texture_cache_hard();
			else
				update_texture_cache_soft();
		}
	}

	if (tVBOpt.RENDERMODE < 2) {
		video_hard_render();
	} else {
		C3D_RenderTargetClear(screenTarget, C3D_CLEAR_ALL, 0, 0);
	}

	if (tVBOpt.RENDERMODE > 0) {
		// postproc
		video_soft_render(alt_buf);
		C3D_TexBind(0, &screenTexSoft[alt_buf]);
		C3D_FrameDrawOn(screenTarget);
		C3D_BindProgram(&sFinal);

		C3D_TexEnv *env = C3D_GetTexEnv(0);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_CONSTANT, 0);
		C3D_TexEnvColor(env, 0x808080);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_ADD);

		env = C3D_GetTexEnv(1);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_CONSTANT, 0);
		C3D_TexEnvColor(env, (brightness[1] << 16) | (brightness[2] << 8) | (brightness[3]) | 0xff808080);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_DOT3_RGB);

		C3D_ImmDrawBegin(GPU_GEOMETRY_PRIM);
		C3D_ImmSendAttrib(1, 1, -1, 1);
		C3D_ImmSendAttrib(1, 1, 0, 0);
		C3D_ImmSendAttrib(-1, -1, -1, 1);
		C3D_ImmSendAttrib(0, 0, 0, 0);
		C3D_ImmDrawEnd();
	}

	// final render
	C3D_TexBind(0, &screenTexHard);
	C3D_BindProgram(&sFinal);
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);

	C3D_TexEnv *env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvColor(env, 0xff0000ff);
	C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
	C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, 0, 0);
	if (minRepeat != maxRepeat) {
		env = C3D_GetTexEnv(1);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_TEXTURE1, 0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
		#ifdef COLTABLESCALE
		C3D_TexEnvScale(env, C3D_RGB, maxRepeat <= 2 ? maxRepeat - 1 : GPU_TEVSCALE_4);
		#endif
	} else {
		env = C3D_GetTexEnv(1);
		C3D_TexEnvInit(env);
	}


	for (int eye = 0; eye < eye_count; eye++) {
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

void video_quit() {
	C3D_Fini();
}
