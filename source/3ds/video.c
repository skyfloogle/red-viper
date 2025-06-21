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
#include "vb_gui.h"
#include "replay.h"
#include "periodic.h"
#include "cpp.h"

#include "final_shbin.h"
#include "soft_shbin.h"

#define TOP_SCREEN_WIDTH  400
#define TOP_SCREEN_HEIGHT 240
#define VIEWPORT_WIDTH    384
#define VIEWPORT_HEIGHT   224
#define MAX_DEPTH         16
#define CENTER_OFFSET     (MAX_DEPTH / 2)

static inline u8 clamp255(int x) {
	return x < 255 ? x : 255;
}

#define BRIGHTNESS_FACTOR 1.75
#define GAMMA 0.9

// some stuff copied from vb_dsp.c

VB_DSPCACHE tDSPCACHE; // Array of Display Cache info...
// Keybd Fn's. Had to put it somewhere!

static volatile bool battery_low = false;
void battery_thread(void) {
	// on citra checking the battery floods the logs
	if (!is_citra) {
		u8 charging, battery_level;
		PTMU_GetBatteryChargeState(&charging);
		PTMU_GetBatteryLevel(&battery_level);
		battery_low = !charging && battery_level <= 2;
	}
}

extern int arm_keys;
u32 input_state = 0;
static bool new_3ds = false;
// Read the Controller, Fix Me....
HWORD V810_RControll(bool reset) {
	if (replay_playing()) {
		guiGetInput(true);
		return replay_read();
	}

	if (reset) input_state = 0;

    int ret_keys = 0;
    int key = 0;

#ifdef __3DS__
    key = hidKeysHeld();
	if (!new_3ds) {
		key |= cppKeysHeld();
	}

#else
    ret_keys = arm_keys;
    arm_keys = 0;
#endif
    if (battery_low) ret_keys |= VB_BATERY_LOW;
	for (int i = 0; i < 32; i++) {
		int mod = tVBOpt.CUSTOM_MOD[i];
		if (mod == 0) {
			// normal
			input_state &= ~BIT(i);
			input_state |= (key & BIT(i));
		} else if (mod == 1) {
			// toggle
			int down = hidKeysDown();
			input_state ^= down & BIT(i);
		} else if (mod == 2) {
			// turbo
			if (key & BIT(i)) input_state ^= BIT(i);
			else input_state &= ~BIT(i);
		}
		if (input_state & BIT(i)) ret_keys |= vbkey[i];
	}

	if (key & KEY_TOUCH) ret_keys |= guiGetInput(true);

	if ((ret_keys & VB_LPAD_L) && (ret_keys & VB_LPAD_R)) ret_keys &= ~(VB_LPAD_L | VB_LPAD_R);
	if ((ret_keys & VB_LPAD_U) && (ret_keys & VB_LPAD_D)) ret_keys &= ~(VB_LPAD_U | VB_LPAD_D);
	if ((ret_keys & VB_RPAD_L) && (ret_keys & VB_RPAD_R)) ret_keys &= ~(VB_RPAD_L | VB_RPAD_R);
	if ((ret_keys & VB_RPAD_U) && (ret_keys & VB_RPAD_D)) ret_keys &= ~(VB_RPAD_U | VB_RPAD_D);

    ret_keys = ret_keys|0x0002; // Always set bit1, ctrl ID
    return ret_keys;
}

void clearCache(void) {
    int i;
    tDSPCACHE.BgmPALMod = 1;                // World Palette Changed
    tDSPCACHE.ObjPALMod = 1;                // Obj Palette Changed
    tDSPCACHE.BrtPALMod = 1;                // Britness for Palette Changed
    tDSPCACHE.ObjDataCacheInvalid = 1;      // Object Cache Is invalid
    tDSPCACHE.ObjCacheInvalid = 1;          // Object Cache Is invalid
    for(i = 0; i < 14; i++)
        tDSPCACHE.BGCacheInvalid[i] = 1;    // Object Cache Is invalid
    for (i = 0; i < 2; i++) {
		tDSPCACHE.DDSPDataState[i] = CPU_WROTE; // Direct Screen Draw changed
		for (int j = 0; j < 64; j++) {
			tDSPCACHE.SoftBufWrote[i][j].min = 0;
			tDSPCACHE.SoftBufWrote[i][j].max = 31;
		}
	}
	tDSPCACHE.CharCacheInvalid = true;
	tDSPCACHE.CharCacheForceInvalid = true;
	for (i = 0; i < 2048; i++)
		tDSPCACHE.CharacterCache[i] = true;
	tDSPCACHE.ColumnTableInvalid = true;
}

C3D_RenderTarget *finalScreen[2];

uint8_t maxRepeat = 0, minRepeat = 0;
C3D_Tex columnTableTexture[2];

u8 brightness_lut[256];

int eye_count = 2;

DVLB_s *sFinal_dvlb;
shaderProgram_s sFinal;

DVLB_s *sSoft_dvlb;
shaderProgram_s sSoft;

bool tileVisible[2048];
int blankTile;

void processColumnTable(void) {
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
	minRepeat = newMinRepeat + 1;
	maxRepeat = newMaxRepeat + 1;
	if (minRepeat != maxRepeat) {
		// populate the bottom row of the textures
		for (int t = 0; t < 2; t++) {
			uint8_t *tex = C3D_Tex2DGetImagePtr(&columnTableTexture[t], 0, NULL);
			for (int i = 0; i < 96; i++) {
				tex[(((i & ~0xf) << 3) | ((i & 8) << 3) | ((i & 4) << 2) | ((i & 2) << 1) | (i & 1)) * 3
					+ 2] = brightness_lut[clamp255(tVIPREG.BRTA * (1 + table[t * 512 + (255 - i) * 2]))];
				tex[(((i & ~0xf) << 3) | ((i & 8) << 3) | ((i & 4) << 2) | ((i & 2) << 1) | (i & 1)) * 3
					+ 1] = brightness_lut[clamp255(tVIPREG.BRTB * (1 + table[t * 512 + (255 - i) * 2]))];
				tex[(((i & ~0xf) << 3) | ((i & 8) << 3) | ((i & 4) << 2) | ((i & 2) << 1) | (i & 1)) * 3
					+ 0] = brightness_lut[clamp255((tVIPREG.BRTA + tVIPREG.BRTB + tVIPREG.BRTC) * (1 + table[t * 512 + (255 - i) * 2]))];
			}
		}
	}
}

void video_init(void) {
    #define DISPLAY_TRANSFER_FLAGS                                                                     \
        (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |               \
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4);

	for (int i = 0; i < 256; i++) {
		brightness_lut[i] = clamp255(pow(((float)i) * BRIGHTNESS_FACTOR / 255, GAMMA) * 255);
	}

    gfxSet3D(true);

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
	params.format = GPU_RGB8;
	params.type = GPU_TEX_2D;
	params.onVram = false;
	params.maxLevel = 0;
	for (int i = 0; i < 2; i++) {
		C3D_TexInitWithParams(&columnTableTexture[i], NULL, params);
	}

    video_hard_init();
	video_soft_init();

	// The hardware renderer creates 1 * 0.75MB + 4 * 1MB framebuffers.
	// The 3DS has two 3MB VRAM banks, so for this to work, 3 framebuffers must go into one bank.
	// However, the allocator alternates between banks, so we need to allocate those first,
	// before even the final render targets.
	finalScreen[0] = C3D_RenderTargetCreate(240, 400, GPU_RB_RGB8, -1);
	C3D_RenderTargetSetOutput(finalScreen[0], GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
	finalScreen[1] = C3D_RenderTargetCreate(240, 400, GPU_RB_RGB8, -1);
	C3D_RenderTargetSetOutput(finalScreen[1], GFX_TOP, GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);

	// render one frame on the top screen and vsync to update vtotal
	C3D_FrameBegin(0);
	C3D_FrameDrawOn(finalScreen[0]);
	C3D_RenderTargetClear(finalScreen[0], C3D_CLEAR_COLOR, 0, 0);
	C3D_FrameEnd(0);
	gspWaitForVBlank();

	// not technically video but we gotta do it somewhere
	APT_CheckNew3DS(&new_3ds);
	startPeriodic(battery_thread, 20000000, true);
}

static int g_alt_buf = 0;

void video_render(int alt_buf, bool on_time) {
	if (tVBOpt.ANTIFLICKER && on_time) video_flush(false);
	
	g_alt_buf = alt_buf;

	if (tVBOpt.RENDERMODE > 0) {
		// postproc (can be done early)
		video_soft_render(alt_buf);
	}

	C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_SHORT, 4);
	AttrInfo_AddLoader(attrInfo, 1, GPU_SHORT, 3);

	eye_count = tVBOpt.ANAGLYPH || CONFIG_3D_SLIDERSTATE > 0.0f ? 2 : 1;

	if (tVIPREG.XPCTRL & XPEN) {
		if (tDSPCACHE.CharCacheInvalid) {
			tDSPCACHE.CharCacheInvalid = false;
			if (tVBOpt.RENDERMODE < 2)
				update_texture_cache_hard();
			else
				update_texture_cache_soft();
		}

		if (tVBOpt.RENDERMODE < 2) {
			video_hard_render();
		} else {
			C3D_RenderTargetClear(screenTarget, C3D_CLEAR_ALL, 0, 0);
		}

		// we need to have this cache during rendering
		memset(tDSPCACHE.CharacterCache, 0, sizeof(tDSPCACHE.CharacterCache));
	}

	C3D_BlendingColor(0x80808080);
	if (tVBOpt.ANTIFLICKER && on_time) C3D_AlphaBlend(GPU_BLEND_ADD, 0, GPU_CONSTANT_ALPHA, GPU_ONE_MINUS_CONSTANT_ALPHA, 0, 0);
	video_flush(false);
	C3D_ColorLogicOp(GPU_LOGICOP_COPY);
}

extern bool any_2ds;

float getDepthOffset(bool default_for_both, int eye, bool full_parallax) {
	if (tVBOpt.ANAGLYPH && any_2ds) {
		int depth = tVBOpt.ANAGLYPH_DEPTH;
		return (eye == 0) ? depth : -depth;
	}

    if (default_for_both || CONFIG_3D_SLIDERSTATE == 0) {
        return 0.0f;
    }

    float directionFactor = (eye == 0) ? 1.0f : -1.0f;

    if (!full_parallax) {
		return directionFactor * CONFIG_3D_SLIDERSTATE * (MAX_DEPTH / 2);
    } else {
        return directionFactor * (CONFIG_3D_SLIDERSTATE * MAX_DEPTH) - (directionFactor * CENTER_OFFSET);
    }
}

void video_flush(bool default_for_both) {
	static int orig_eye = 0;
	if (!default_for_both) orig_eye = tVBOpt.DEFAULT_EYE;
	if (eye_count == 2) default_for_both = false;

	if (tDSPCACHE.ColumnTableInvalid)
		processColumnTable();

	C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_SHORT, 4);
	AttrInfo_AddLoader(attrInfo, 1, GPU_SHORT, 3);

	C3D_TexBind(0, &screenTexHard);
	C3D_TexBind(1, &screenTexSoft[g_alt_buf]);
	C3D_BindProgram(&sFinal);
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);

	C3D_TexEnv *env;
	// If drawing VIP on top of softbuf, create a mask of the VIP graphics by adding all 3 channels.
	if ((tDSPCACHE.DDSPDataState[g_alt_buf] != GPU_CLEAR && tVBOpt.VIP_OVER_SOFT)) {
		env = C3D_GetTexEnv(0);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_TEXTURE0, 0);
		C3D_TexEnvOpAlpha(env, GPU_TEVOP_A_SRC_R, GPU_TEVOP_A_SRC_G, 0);
		C3D_TexEnvFunc(env, C3D_Alpha, GPU_ADD);
		env = C3D_GetTexEnv(1);
		C3D_TexEnvInit(env);
		C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS, GPU_TEXTURE0, 0);
		C3D_TexEnvOpAlpha(env, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_B, 0);
		C3D_TexEnvFunc(env, C3D_Alpha, GPU_ADD);
	} else {
		C3D_TexEnvInit(C3D_GetTexEnv(0));
		env = C3D_GetTexEnv(1);
		C3D_TexEnvInit(env);
		if (tDSPCACHE.DDSPDataState[g_alt_buf] != GPU_CLEAR) {
			// Appears to be necessary, I wish I could tell you why.
			C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE1, 0, 0);
		}
	}

	// Draw the softbuf onto the VIP, or vice versa.
	env = C3D_GetTexEnv(2);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_TEXTURE1, GPU_TEXTURE1);
	if (tDSPCACHE.DDSPDataState[g_alt_buf] != GPU_CLEAR) {
		if (tVBOpt.VIP_OVER_SOFT) {
			C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE1, GPU_PREVIOUS, GPU_TEXTURE0);
		}
		C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA, GPU_TEVOP_RGB_SRC_COLOR);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
		C3D_TexEnvFunc(env, C3D_Alpha, GPU_ADD);
	}

	// Apply brightness values to the three channels, possibly using the column table.
	env = C3D_GetTexEnv(3);
	C3D_TexEnvInit(env);
	if (minRepeat != maxRepeat) {
		C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_TEXTURE2, 0);
	} else {
		C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_CONSTANT, 0);
		// brightness 1, 2, 3 into r, g, b
		C3D_TexEnvColor(env,
			(brightness_lut[clamp255(tVIPREG.BRTA * maxRepeat)]) |
			(brightness_lut[clamp255(tVIPREG.BRTB * maxRepeat)] << 8) |
			(brightness_lut[clamp255((tVIPREG.BRTA + tVIPREG.BRTB + tVIPREG.BRTC) * maxRepeat)] << 16)
		);
	}
	C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
	C3D_TexEnvBufUpdate(C3D_RGB, 1 << 3);

	// Merge the first two brightness values.
	env = C3D_GetTexEnv(4);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_PREVIOUS, 0);
	C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_R, GPU_TEVOP_RGB_SRC_G, 0);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_ADD);

	// Merge in the third brightness value, then tint (if not anaglyph).
	env = C3D_GetTexEnv(5);
	C3D_TexEnvInit(env);
	C3D_TexEnvColor(env, tVBOpt.TINT);
	C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_PREVIOUS_BUFFER, GPU_CONSTANT);
	C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_B, GPU_TEVOP_RGB_SRC_COLOR);
	C3D_TexEnvFunc(env, C3D_RGB, tVBOpt.ANAGLYPH ? GPU_ADD : GPU_ADD_MULTIPLY);
	
	int viewportX = (TOP_SCREEN_WIDTH - VIEWPORT_WIDTH) / 2;
	int viewportY = (TOP_SCREEN_HEIGHT - VIEWPORT_HEIGHT) / 2;

	for (int dst_eye = 0; dst_eye < (default_for_both ? 2 : eye_count); dst_eye++) {
		int src_eye = default_for_both ? orig_eye : !tVBOpt.ANAGLYPH && CONFIG_3D_SLIDERSTATE == 0 ? tVBOpt.DEFAULT_EYE : dst_eye;
		if (tVBOpt.ANAGLYPH) {
			C3D_DepthTest(false, GPU_ALWAYS, (src_eye ? tVBOpt.ANAGLYPH_RIGHT : tVBOpt.ANAGLYPH_LEFT) | GPU_WRITE_ALPHA);
		}
		float depthOffset = getDepthOffset(default_for_both, dst_eye, tVBOpt.SLIDERMODE);
		C3D_RenderTarget *target = finalScreen[dst_eye && !tVBOpt.ANAGLYPH];
		C3D_RenderTargetClear(target, C3D_CLEAR_ALL, 0, 0);
		C3D_FrameDrawOn(target);
		C3D_SetViewport(viewportY, viewportX+depthOffset, VIEWPORT_HEIGHT, VIEWPORT_WIDTH);
		C3D_TexBind(2, &columnTableTexture[src_eye]);
		C3D_ImmDrawBegin(GPU_GEOMETRY_PRIM);
		C3D_ImmSendAttrib(1, 1, -1, 1);
		C3D_ImmSendAttrib(0, src_eye ? 0.5 : 0, 0, 0);
		C3D_ImmSendAttrib(-1, -1, -1, 1);
		C3D_ImmSendAttrib(384.0 / 512, (src_eye ? 0.5 : 0) + 224.0 / 512, 0, 0);
		C3D_ImmDrawEnd();
	}
	
	if (tVBOpt.ANAGLYPH) {
		C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
	}

	for (int i = 0; i <= 5; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
}

void video_quit(void) {
	C3D_Fini();
}
