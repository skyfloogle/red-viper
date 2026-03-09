#include "vb_dsp.h"
#include "v810_mem.h"
#include "vb_set.h"
#include "video_hard.h"
#include "n3ds_shaders.h"

#include <tex3ds.h>
#include "palette_mask_t3x.h"
#include "palette_mask_tocpu_t3x.h"
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

static C3D_Tex palette_mask, palette_mask_tocpu;

static C3D_FVec pal1tex[8] = {0}, pal2tex[8] = {0}, pal3col[8] = {0};

static LightEvent transfer_event;
static volatile int ppfCount = 0;
static bool downloaded = false;
static void ppfCallback(void *data) {
	if (AtomicIncrement(&ppfCount) < 2) LightEvent_Signal(&transfer_event);
}

void gpu_init(void) {
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

	Tex3DS_TextureFree(Tex3DS_TextureImport(palette_mask_t3x, palette_mask_t3x_size, &palette_mask, NULL, false));
	Tex3DS_TextureFree(Tex3DS_TextureImport(palette_mask_tocpu_t3x, palette_mask_tocpu_t3x_size, &palette_mask_tocpu, NULL, false));

	C3D_TexSetFilter(&tileTexture, GPU_NEAREST, GPU_NEAREST);

	C3D_ColorLogicOp(GPU_LOGICOP_COPY);

	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);

	gspSetEventCallback(GSPGPU_EVENT_PPF, ppfCallback, NULL, false);
	LightEvent_Init(&transfer_event, RESET_ONESHOT);
}

void gpu_setup_tile_drawing(void) {
	C3D_BindProgram(&sChar);

	C3D_BufInfo *bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, vbuf, sizeof(vertex), 2, 0x10);

	C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_SHORT, 2);
	AttrInfo_AddLoader(attrInfo, 1, GPU_BYTE, 4);

	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc.posscale, 1.0 / (512 / 2), 1.0 / (512 / 2), -1.0, 1.0);
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc.offset, 0, 0, 0, 0);
	memcpy(C3D_FVUnifWritePtr(GPU_GEOMETRY_SHADER, uLoc.pal1tex, 8), pal1tex, sizeof(pal1tex));
	memcpy(C3D_FVUnifWritePtr(GPU_GEOMETRY_SHADER, uLoc.pal2tex, 8), pal2tex, sizeof(pal2tex));
	memcpy(C3D_FVUnifWritePtr(GPU_GEOMETRY_SHADER, uLoc.pal3col, 8), pal3col, sizeof(pal3col));

	C3D_TexBind(0, &tileTexture);
	C3D_TexBind(1, &palette_mask);
	C3D_TexBind(2, &palette_mask);

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
}

void gpu_set_tile_offset(float xoffset, float yoffset) {
	C3D_FVUnifSet(GPU_VERTEX_SHADER, uLoc.offset, xoffset, yoffset, 0, 0);
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

void gpu_init_vip_download(int previous_transfer_count, int start_eye, int end_eye, int drawn_fb) {
    C3D_FrameSplit(0);
    ppfCount = -previous_transfer_count;
    downloaded = false;
    for (int eye = start_eye; eye < end_eye; eye++) {
        GX_DisplayTransfer(
            screenTexHard[drawn_fb].data
                + ((8*8*2)*(256/8) * eye)
                + (512-384)*512*2, // line things up
            GX_BUFFER_DIM(512, 384),
            (u32*)(
                rgba4_framebuffers
                + (384 * DOWNLOADED_FRAMEBUFFER_WIDTH * eye)
                - ((512-DOWNLOADED_FRAMEBUFFER_WIDTH)*383) // account for weird offset when vflip and cropping are both on
            ),
            GX_BUFFER_DIM(DOWNLOADED_FRAMEBUFFER_WIDTH, 384),
            GX_TRANSFER_FLIP_VERT(1) | 4 | GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA4) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA4));
    }
}

void video_download_vip(int drawn_fb) {
	if (tVBOpt.RENDERMODE != RM_TOCPU) return;
	if (downloaded) return;
	downloaded = true;
	while (ppfCount < 0) LightEvent_Wait(&transfer_event);
	int eye = 0;
	while (eye < 2) {
		if (ppfCount < eye) LightEvent_Wait(&transfer_event);
		uint32_t *in_fb = (uint32_t*)(rgba4_framebuffers + (384 * DOWNLOADED_FRAMEBUFFER_WIDTH) * eye);
		uint32_t *out_fb = (uint32_t*)(vb_state->V810_DISPLAY_RAM.off + 0x10000 * eye + 0x8000 * drawn_fb);
		GSPGPU_FlushDataCache(in_fb, 384*DOWNLOADED_FRAMEBUFFER_WIDTH*2);
		for (int x = 0; x < 384; x++) {
			for (int y = 0; y < 224; y += (32/2)) {
				uint32_t buf = 0;
				for (int i = 0; i < (32/4); i++) {
					uint32_t inbuf = *in_fb++;
					buf |= (((inbuf & 0xffff) >> 8) | (inbuf >> 22)) << (i*4);
				}
				*out_fb++ = buf;
			}
			in_fb += (DOWNLOADED_FRAMEBUFFER_WIDTH - 224) / 2;
			out_fb += (256 - 224) / 4 / sizeof(out_fb[0]);
		}
		eye++;
	}
	tDSPCACHE.DDSPDataState[drawn_fb] = CPU_WROTE;
	for (int i = 0; i < 64; i++) {
		tDSPCACHE.SoftBufWrote[drawn_fb][i].min = 0;
		tDSPCACHE.SoftBufWrote[drawn_fb][i].max = 31;
	}
}

void gpu_clear_screen(int start_eye, int end_eye) {
	C3D_BindProgram(&sFinal);
	gpu_set_opaque(true);

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
	// note: output position is transposed
	// left
	if (start_eye == 0) {
		C3D_ImmSendAttrib(224.0/256-1, 384.0/256-1, -1, -1);
		C3D_ImmSendAttrib(0, 0, 0, 0);
		C3D_ImmSendAttrib(0, 0, 0, 0);
	}
	// right
	if (end_eye == 2) {
		C3D_ImmSendAttrib(224.0/256, 384.0/256-1, 0, -1);
		C3D_ImmSendAttrib(0, 0, 0, 0);
		C3D_ImmSendAttrib(0, 0, 0, 0);
	}
	C3D_ImmDrawEnd();
	gpu_set_opaque(false);
}

void gpu_setup_drawing(void) {
	for (int i = 0; i < 4; i++) {
		const C3D_FVec normal_cols[4] = {{}, {.x = 1}, {.y = 1}, {.z = 1}};
		const C3D_FVec tocpu_cols[4] = {{}, {.y = 1.0/16}, {.y = 2.0/16}, {.y = 3.0/16}};
		const C3D_FVec *cols = tVBOpt.RENDERMODE != RM_TOCPU ? normal_cols : tocpu_cols;
		HWORD pal = vb_state->tVIPREG.GPLT[i];
		pal1tex[i].x = (((pal >> 1) & 0b110) + 1) / 8.0;
		pal2tex[i].x = (((pal >> 3) & 0b110) + 1) / 8.0;
		memcpy(&pal3col[i], &cols[(pal >> 6) & 3], sizeof(C3D_FVec));
		pal = vb_state->tVIPREG.JPLT[i];
		pal1tex[i + 4].x = (((pal >> 1) & 0b110) + 1) / 8.0;
		pal2tex[i + 4].x = (((pal >> 3) & 0b110) + 1) / 8.0;
		memcpy(&pal3col[i + 4], &cols[(pal >> 6) & 3], sizeof(C3D_FVec));
	}

	C3D_CullFace(GPU_CULL_NONE);
}

void gpu_set_target(Framebuffer target) {
    C3D_FrameDrawOn(target);
}

void gpu_set_scissor(bool enabled, u32 left, u32 top, u32 right, u32 bottom) {
    C3D_SetScissor(enabled ? GPU_SCISSOR_NORMAL : GPU_SCISSOR_DISABLE, left, top, right, bottom);
}

void gpu_set_opaque(bool opaque) {
    C3D_AlphaTest(!opaque, GPU_GREATER, 0);
}

void gpu_draw_tiles(int first, int count) {
    if (count != 0) C3D_DrawArrays(GPU_GEOMETRY_PRIM, first, count);
}
