#include <string.h>
#include <3ds.h>
#include <citro3d.h>

#include "3ds/services/hid.h"
#include "c3d/renderqueue.h"
#include "c3d/texenv.h"
#include "c3d/texture.h"
#include "utils.h"

#include "vb_dsp.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_set.h"
#include "vb_gui.h"
#include "replay.h"
#include "periodic.h"
#include "cpp.h"

#include "n3ds_shaders.h"

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
		circlePosition cpp;
		cppCircleRead(&cpp);
		if (cpp.dx >= 41) key |= KEY_CSTICK_RIGHT;
		else if (cpp.dx <= -41) key |= KEY_CSTICK_LEFT;
		if (cpp.dy >= 41) key |= KEY_CSTICK_UP;
		else if (cpp.dy <= -41) key |= KEY_CSTICK_DOWN;
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

C3D_RenderTarget *finalScreen[2];

static float *final_vbuf;
static uint32_t *coltable_vbuf;

static C3D_ProcTex procTex;
static C3D_ProcTexLut procTexLut;
static C3D_ProcTexColorLut procTexColorLut;

uint8_t maxRepeat = 0, minRepeat = 0;
C3D_Tex columnTableTexture;

u8 brightness_lut[256];

int eye_count = 2;

static int get_colour(int id, int brt_reg) {
	if (id == 0) {
		return tVBOpt.MULTICOL && !tVBOpt.ANAGLYPH ? tVBOpt.MTINT[tVBOpt.MULTIID][0] : 0;
	}
	int brightness = clamp255(brightness_lut[clamp255(brt_reg)] * (tVBOpt.MULTICOL && !tVBOpt.ANAGLYPH ? tVBOpt.STINT[tVBOpt.MULTIID][id - 1] : 1));
	int fulltint = tVBOpt.ANAGLYPH ? 0xffffff : tVBOpt.MULTICOL ? tVBOpt.MTINT[tVBOpt.MULTIID][id] : tVBOpt.TINT;
	int col_tint =
		((brightness * ((fulltint) & 0xff) / 255)) |
		((brightness * ((fulltint >> 8) & 0xff) / 255) << 8) |
		((brightness * ((fulltint >> 16) & 0xff) / 255) << 16);
	if (tVBOpt.ANAGLYPH || !tVBOpt.MULTICOL) return col_tint;

	int black_brightness = 255 - brightness;
	int black_tint = tVBOpt.ANAGLYPH ? 0 :
		((black_brightness * ((tVBOpt.MTINT[tVBOpt.MULTIID][0]) & 0xff) / 255)) |
		((black_brightness * ((tVBOpt.MTINT[tVBOpt.MULTIID][0] >> 8) & 0xff) / 255) << 8) |
		((black_brightness * ((tVBOpt.MTINT[tVBOpt.MULTIID][0] >> 16) & 0xff) / 255) << 16);
	
	return __builtin_arm_uqadd8(col_tint, black_tint);
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

	n3ds_shaders_init();

	coltable_vbuf = linearAlloc(4 * 96 * 2);
	final_vbuf = linearAlloc(sizeof(float) * 4 * 2 * 96 * 2);
	for (int i = 0; i < 96; i++) {
		for (int src_eye = 0; src_eye < 2; src_eye++) {
			final_vbuf[src_eye*8*96+i*8] = 1;
			final_vbuf[src_eye*8*96+i*8+1] = ((96 - i) * 2 - 96) / 96.0;
			final_vbuf[src_eye*8*96+i*8+2] = -1;
			final_vbuf[src_eye*8*96+i*8+3] = ((95 - i) * 2 - 96) / 96.0;
			final_vbuf[src_eye*8*96+i*8+4] = i * 4 / 512.0;
			final_vbuf[src_eye*8*96+i*8+5] = src_eye ? 0.5 : 0;
			final_vbuf[src_eye*8*96+i*8+6] = (i + 1) * 4 / 512.0;
			final_vbuf[src_eye*8*96+i*8+7] = (src_eye ? 0.5 : 0) + 224.0 / 512;
		}
	}

	// we only need 96*2 values, but add some more to avoid the edges
	C3D_ProcTexInit(&procTex, 0, 96*2+3);
	C3D_ProcTexClamp(&procTex, GPU_PT_REPEAT, 0);
	C3D_ProcTexCombiner(&procTex, false, GPU_PT_U, GPU_PT_U);
	C3D_ProcTexShift(&procTex, GPU_PT_NONE, GPU_PT_NONE);
	C3D_ProcTexFilter(&procTex, GPU_PT_NEAREST);
	
	float linearLutData[129];
	for (int i = 0; i <= 128; i++) {
		// reading a 96*2+3 lut from a 256-"pixel" texture
		// 1/2048 was subtracted to avoid weird noise on hardware
		linearLutData[i] = (i / 128.0) * 256.0 / (96*2+2) - 1/512.0 - 1/2048.0;
		// the right half should read from halfway through the lut
		if (i >= 64) linearLutData[i] -= 43.0 / 256.0 - 3/1024.0 - 1/2048.0;
	}
	ProcTexLut_FromArray(&procTexLut, linearLutData);

	C3D_TexInitParams params;
	params.width = 256;
	params.height = 8;
	params.format = GPU_RGB8;
	params.type = GPU_TEX_2D;
	params.onVram = false;
	params.maxLevel = 0;
	for (int i = 0; i < 2; i++) {
		C3D_TexInitWithParams(&columnTableTexture, NULL, params);
	}

    video_hard_init();
	video_soft_init();

	// The hardware renderer creates 1 * 0.75MB + 8 * 0.5MB framebuffers.
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

void processColumnTable(void) {
	u8 *table = (u8*)vb_state->V810_DISPLAY_RAM.off + 0x3dc01;
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
		u32 coltable_proctex[96*2+1];
		// populate the bottom row of the textures
		uint8_t *tex = C3D_Tex2DGetImagePtr(&columnTableTexture, 0, NULL);
		for (int t = 0; t < 2; t++) {
			for (int i = 0; i < 96; i++) {
				int col_a = get_colour(1, vb_state->tVIPREG.BRTA * (1 + table[t * 512 + (255 - i) * 2]));
				int col_b = get_colour(2, vb_state->tVIPREG.BRTB * (1 + table[t * 512 + (255 - i) * 2]));
				int col_c = get_colour(3, (vb_state->tVIPREG.BRTA + vb_state->tVIPREG.BRTB + vb_state->tVIPREG.BRTC) * (1 + table[t * 512 + (255 - i) * 2]));

				u8 *px = &tex[(128*8*t+((((i+1) & ~0xf) << 3) | (((i+1) & 8) << 3) | (((i+1) & 4) << 2) | (((i+1) & 2) << 1) | ((i+1) & 1))) * 3];
				px[2] = (col_a) & 0xff;
				px[1] = (col_a >> 8) & 0xff;
				px[0] = (col_a >> 16);

				coltable_proctex[t*96+i+1] = col_b;
				coltable_vbuf[t*96+i] = col_c;
			}
		}
		// 1 pixel from the leftmost entry still leaks in
		coltable_proctex[0] = coltable_proctex[1];
		ProcTexColorLut_Write(&procTexColorLut, coltable_proctex, 0, 96*2+1);
	}
}

static int g_displayed_fb = 0;
static int vip_displayed_fb = 0;

void video_render(int displayed_fb, bool on_time) {
	if (tVBOpt.ANTIFLICKER && on_time) video_flush(false);
	
	g_displayed_fb = displayed_fb;
	vip_displayed_fb = tVBOpt.DOUBLE_BUFFER ? displayed_fb : 0;

	if (tVBOpt.RENDERMODE != RM_GPUONLY) {
		// postproc (can be done early)
		video_soft_to_texture(displayed_fb);
	}

	if (tVBOpt.DOUBLE_BUFFER) {
		C3D_BlendingColor(0x80808080);
		if (tVBOpt.ANTIFLICKER && on_time) C3D_AlphaBlend(GPU_BLEND_ADD, 0, GPU_CONSTANT_ALPHA, GPU_ONE_MINUS_CONSTANT_ALPHA, 0, 0);
		video_flush(false);
		C3D_ColorLogicOp(GPU_LOGICOP_COPY);
	}

	C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 4);
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 4);
	AttrInfo_AddLoader(attrInfo, 2, GPU_UNSIGNED_BYTE, 4);

	eye_count = tVBOpt.ANAGLYPH || CONFIG_3D_SLIDERSTATE > 0.0f ? 2 : 1;

	if (vb_state->tVIPREG.XPCTRL & XPEN) {
		if (tDSPCACHE.CharCacheInvalid) {
			if (tVBOpt.RENDERMODE != RM_CPUONLY)
				update_texture_cache_hard();
			else
				update_texture_cache_soft();
		}

		if (tVBOpt.RENDERMODE != RM_CPUONLY) {
			video_hard_render(tVBOpt.DOUBLE_BUFFER ? !displayed_fb : 0);
		} else {
			C3D_RenderTargetClear(screenTargetHard[!vip_displayed_fb], C3D_CLEAR_ALL, 0, 0);
			video_soft_render(!displayed_fb);
		}

		// we need to have these caches during rendering
		tDSPCACHE.CharCacheInvalid = false;
		memset(tDSPCACHE.BGCacheInvalid, 0, sizeof(tDSPCACHE.BGCacheInvalid));
		memset(tDSPCACHE.CharacterCache, 0, sizeof(tDSPCACHE.CharacterCache));
	}

	if (!tVBOpt.DOUBLE_BUFFER) {
		C3D_BlendingColor(0x80808080);
		if (tVBOpt.ANTIFLICKER && on_time) C3D_AlphaBlend(GPU_BLEND_ADD, 0, GPU_CONSTANT_ALPHA, GPU_ONE_MINUS_CONSTANT_ALPHA, 0, 0);
		video_flush(false);
		C3D_ColorLogicOp(GPU_LOGICOP_COPY);
	}
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

	if (tDSPCACHE.ColumnTableInvalid || (minRepeat != maxRepeat && tDSPCACHE.BrtPALMod))
		processColumnTable();

	C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 4);
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 4);
	AttrInfo_AddLoader(attrInfo, 2, GPU_UNSIGNED_BYTE, 4);

	C3D_TexBind(0, &screenTexHard[vip_displayed_fb]);
	C3D_TexBind(1, &screenTexSoft[g_displayed_fb]);
	C3D_BindProgram(&sFinal);
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);

	C3D_TexEnv *env;
	// If drawing VIP on top of softbuf, create a mask of the VIP graphics by adding all 3 channels.
	if (tVBOpt.RENDERMODE != RM_CPUONLY && (tDSPCACHE.DDSPDataState[g_displayed_fb] != GPU_CLEAR && tVBOpt.VIP_OVER_SOFT)) {
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
		if (tDSPCACHE.DDSPDataState[g_displayed_fb] != GPU_CLEAR) {
			// Appears to be necessary, I wish I could tell you why.
			C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE1, 0, 0);
		}
	}

	// Draw the softbuf onto the VIP, or vice versa.
	env = C3D_GetTexEnv(2);
	C3D_TexEnvInit(env);
	if (tVBOpt.RENDERMODE != RM_CPUONLY) {
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_TEXTURE1, GPU_TEXTURE1);
		if (tDSPCACHE.DDSPDataState[g_displayed_fb] != GPU_CLEAR) {
			if (tVBOpt.VIP_OVER_SOFT) {
				C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE1, GPU_PREVIOUS, GPU_TEXTURE0);
			}
			C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA, GPU_TEVOP_RGB_SRC_COLOR);
			C3D_TexEnvFunc(env, C3D_RGB, GPU_MULTIPLY_ADD);
		}
	} else {
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE1, 0, 0);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
	}
	C3D_TexEnvColor(env, -1);
	C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, 0, 0);
	C3D_TexEnvBufUpdate(C3D_RGB, 1 << 2);

	C3D_TexEnvBufColor(get_colour(0, 0));
	env = C3D_GetTexEnv(3);
	C3D_TexEnvInit(env);
	if (minRepeat == maxRepeat)
		C3D_TexEnvColor(env, get_colour(1, vb_state->tVIPREG.BRTA * maxRepeat));
	C3D_TexEnvSrc(env, C3D_RGB, minRepeat == maxRepeat ? GPU_CONSTANT : GPU_TEXTURE2, GPU_PREVIOUS_BUFFER, GPU_PREVIOUS);
	C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_R);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);

	env = C3D_GetTexEnv(4);
	C3D_TexEnvInit(env);
	if (minRepeat == maxRepeat)
		C3D_TexEnvColor(env, get_colour(2, vb_state->tVIPREG.BRTB * maxRepeat));
	C3D_TexEnvSrc(env, C3D_RGB, minRepeat == maxRepeat ? GPU_CONSTANT : GPU_TEXTURE3, GPU_PREVIOUS, GPU_PREVIOUS_BUFFER);
	C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_G);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);

	env = C3D_GetTexEnv(5);
	C3D_TexEnvInit(env);
	if (minRepeat == maxRepeat)
		C3D_TexEnvColor(env, get_colour(3, (vb_state->tVIPREG.BRTA + vb_state->tVIPREG.BRTB + vb_state->tVIPREG.BRTC) * maxRepeat));
	C3D_TexEnvSrc(env, C3D_RGB, minRepeat == maxRepeat ? GPU_CONSTANT : GPU_PRIMARY_COLOR, GPU_PREVIOUS, GPU_PREVIOUS_BUFFER);
	C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_B);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
	
	int viewportX = (TOP_SCREEN_WIDTH - VIEWPORT_WIDTH) / 2;
	int viewportY = (TOP_SCREEN_HEIGHT - VIEWPORT_HEIGHT) / 2;

	C3D_BufInfo *bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, final_vbuf, sizeof(float)*8, 2, 0x10);
	BufInfo_Add(bufInfo, coltable_vbuf, 4, 1, 0x2);

	C3D_TexBind(2, &columnTableTexture);
	C3D_ProcTexBind(2, &procTex);
	C3D_ProcTexLutBind(GPU_LUT_RGBMAP, &procTexLut);
	C3D_ProcTexLutBind(GPU_LUT_ALPHAMAP, &procTexLut);
	C3D_ProcTexLutBind(GPU_LUT_NOISE, &procTexLut);
	C3D_ProcTexColorLutBind(&procTexColorLut);

	C3D_AlphaTest(false, GPU_GREATER, 0);

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
		C3D_FVUnifSet(GPU_GEOMETRY_SHADER, uLoc.shading_offset, (src_eye ? 0.5 : 0) + 1/256.0, 0, 0, 0);
		C3D_DrawArrays(GPU_GEOMETRY_PRIM, src_eye*96, 96);
	}
	
	if (tVBOpt.ANAGLYPH) {
		C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
	}

	C3D_AlphaTest(true, GPU_GREATER, 0);

	for (int i = 0; i <= 5; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));

	// cleanup
	tDSPCACHE.ColumnTableInvalid = false;
	tDSPCACHE.BrtPALMod = false;
}

void video_quit(void) {
	C3D_Fini();
}
