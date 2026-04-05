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

#define USE_SOFT_FLUSH false

#define TOP_SCREEN_WIDTH  400
#define TOP_SCREEN_HEIGHT 240
#define VIEWPORT_WIDTH    384
#define VIEWPORT_HEIGHT   224
#define MAX_DEPTH         16
#define CENTER_OFFSET     (MAX_DEPTH / 2)

static C3D_Tex affine_masks[4][4];
static C3D_FVec bgmap_offsets[2];

C3D_Tex screenTexHard[2];
C3D_RenderTarget *screenTargetHard[2];

static C3D_Tex tileTexture;

static C3D_Tex palette_mask, palette_mask_tocpu;

static C3D_FVec pal1tex[8] = {0}, pal2tex[8] = {0}, pal3col[8] = {0};

static C3D_RenderTarget *finalScreen[2];

static float *final_vbuf;
static uint32_t *coltable_vbuf;

static C3D_ProcTex procTex;
static C3D_ProcTexLut procTexLut;
static C3D_ProcTexColorLut procTexColorLut;

uint8_t maxRepeat = 0, minRepeat = 0;
C3D_Tex columnTableTexture;

static LightEvent transfer_event;
static volatile int ppfCount = 0;
static bool downloaded = false;
static void ppfCallback(void *data) {
	if (AtomicIncrement(&ppfCount) < 2) LightEvent_Signal(&transfer_event);
}

void gpu_init(void) {
    #define DISPLAY_TRANSFER_FLAGS                                                                     \
        (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |               \
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) | \
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 4);

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

	params.width = 256;
	params.height = 512;
	params.format = GPU_RGBA4;
	params.type = GPU_TEX_2D;
	params.onVram = false;
	params.maxLevel = 0;
	C3D_TexInitWithParams(&tileTexture, NULL, params);

	// We have ten 16-bit 512x512 textures, adding up to 5MB of the available 6MB.
	// On top of that, we also have one 320x240x3 and two 400x240x4 textures.
	// The 3DS has two 3MB VRAM banks, and if we just allocate back and forth,
	// there won't be room in either for the remaining textures.
	// To make sure we have room in one bank, we allocate a 1MB block of VRAM
	// and free it after allocating the textures.
	void *tempAlloc = vramAlloc(1024*1024);

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
	for (int i = 0; i < AFFINE_CACHE_SIZE; i++) {
		C3D_TexInitWithParams(&tileMapCache[i].tex, NULL, params);
		tileMapCache[i].target = C3D_RenderTargetCreateFromTex(&tileMapCache[i].tex, GPU_TEXFACE_2D, 0, -1);
		C3D_RenderTargetClear(tileMapCache[i].target, C3D_CLEAR_ALL, 0xffffffff, 0);
		tileMapCache[i].bg = -1;
	}

	vramFree(tempAlloc);

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
	Tex3DS_TextureFree(Tex3DS_TextureImport(palette_mask_tocpu_t3x, palette_mask_tocpu_t3x_size, &palette_mask_tocpu, NULL, false));

	C3D_TexSetFilter(&tileTexture, GPU_NEAREST, GPU_NEAREST);

	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);

	gspSetEventCallback(GSPGPU_EVENT_PPF, ppfCallback, NULL, false);
	LightEvent_Init(&transfer_event, RESET_ONESHOT);

	video_soft_init();

	finalScreen[0] = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, -1);
	C3D_RenderTargetSetOutput(finalScreen[0], GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
	finalScreen[1] = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, -1);
	C3D_RenderTargetSetOutput(finalScreen[1], GFX_TOP, GFX_RIGHT, DISPLAY_TRANSFER_FLAGS);

	// render one frame on the top screen and vsync to update vtotal
	C3D_FrameBegin(0);
	C3D_FrameDrawOn(finalScreen[0]);
	C3D_RenderTargetClear(finalScreen[0], C3D_CLEAR_COLOR, 0, 0);
	C3D_FrameEnd(0);
	gspWaitForVBlank();
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
	C3D_TexBind(1, tVBOpt.RENDERMODE != RM_TOCPU ? &palette_mask : &palette_mask_tocpu);
	C3D_TexBind(2, tVBOpt.RENDERMODE != RM_TOCPU ? &palette_mask : &palette_mask_tocpu);

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

static void draw_affine_layer(int drawn_fb, avertex *vbufs[], C3D_Tex **textures, int count, int base_gx, int gp, int gy, int w, int h, bool use_masks) {
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
	AttrInfo_AddLoader(attrInfo, 3, GPU_SHORT, 2);

	C3D_BufInfo *bufInfo = C3D_GetBufInfo();
	BufInfo_Init(bufInfo);
	BufInfo_Add(bufInfo, avbuf, sizeof(avertex), 4, 0x3210);

	for (int eye = 0; eye < 2; eye++) {
		if (vbufs[eye] != NULL) {
			int gx = base_gx + (eye == 0 ? -gp : gp);
			
			// note: transposed
			gpu_set_scissor(true, 256 * eye + (gy >= 0 ? gy : 0), gx >= 0 ? gx : 0, (gy + h < 256 ? gy + h : 256) + 256 * eye, gx + w);
			C3D_DrawArrays(GPU_GEOMETRY_PRIM, vbufs[eye] - avbuf, h);
		}
	}
}

void gpu_draw_affine(WORLD *world, int umin, int vmin, int umax, int vmax, int drawn_fb, avertex *vbufs[], bool visible[]) {
	uint8_t mapid = world->head & 0xf;
	uint8_t scx_pow = ((world->head >> 10) & 3);
	uint8_t scy_pow = ((world->head >> 8) & 3);
	uint8_t map_count_pow = scx_pow + scy_pow;
	bool huge_bg = map_count_pow > 3;
	if (huge_bg) map_count_pow = 3;
	uint8_t scx = 1 << scx_pow;
	uint8_t scy = 1 << scy_pow;
	uint8_t map_count = 1 << map_count_pow;
	mapid &= ~(map_count - 1);
	bool over = world->head & 0x80;
	int16_t base_gx = (s16)(world->gx << 6) >> 6;
	int16_t gp = (s16)(world->gp << 6) >> 6;
	int16_t gy = world->gy;
	int16_t w = world->w + 1;
	int16_t h = world->h + 1;
	int16_t over_tile = world->over & 0x7ff;

	u16 *tilemap = (u16 *)(vb_state->V810_DISPLAY_RAM.off + 0x20000);

	int full_w = 512 * scx;
	int full_h = 512 * scy;

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
				if ((world->head & 0x3000) == 0x1000)
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

static int previous_transfer_count;

void gpu_reset_vip_download(void) {
	previous_transfer_count = 0;
}

void gpu_init_vip_download(int start_eye, int end_eye, int drawn_fb) {
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
	int eye = 0;
	while (eye < 2) {
		while (ppfCount <= eye) LightEvent_Wait(&transfer_event);
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

void gpu_soft_to_texture(int displayed_fb) {
	#if !USE_SOFT_FLUSH
	video_soft_to_texture(displayed_fb);
	#endif
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

void gpu_target_screen(int drawn_fb) {
	C3D_FrameDrawOn(screenTargetHard[drawn_fb]);
}

void gpu_target_affine(int cache_id) {
	C3D_FrameDrawOn(tileMapCache[cache_id].target);
	gpu_set_scissor(false, 0, 0, 0, 0);
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

static int orig_eye = 0;

static void video_flush_hard(bool default_for_both, int displayed_fb, int vip_displayed_fb) {
	C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 4);
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 4);
	AttrInfo_AddLoader(attrInfo, 2, GPU_UNSIGNED_BYTE, 4);

	C3D_TexBind(0, &screenTexHard[vip_displayed_fb]);
	C3D_TexBind(1, &screenTexSoft[displayed_fb]);
	C3D_BindProgram(&sFinal);
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);

	C3D_TexEnv *env;
	// If drawing VIP on top of softbuf, create a mask of the VIP graphics by adding all 3 channels.
	if (tVBOpt.RENDERMODE != RM_TOCPU && tVBOpt.RENDERMODE != RM_CPUONLY && (tDSPCACHE.DDSPDataState[displayed_fb] != GPU_CLEAR && tVBOpt.VIP_OVER_SOFT)) {
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
		if (tDSPCACHE.DDSPDataState[displayed_fb] != GPU_CLEAR) {
			// Appears to be necessary, I wish I could tell you why.
			C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE1, 0, 0);
		}
	}

	// Draw the softbuf onto the VIP, or vice versa.
	env = C3D_GetTexEnv(2);
	C3D_TexEnvInit(env);
	if (tVBOpt.RENDERMODE != RM_TOCPU && tVBOpt.RENDERMODE != RM_CPUONLY) {
		C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_TEXTURE1, GPU_TEXTURE1);
		if (tDSPCACHE.DDSPDataState[displayed_fb] != GPU_CLEAR) {
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

	C3D_TexEnvBufColor(video_get_colour(0, 0));
	env = C3D_GetTexEnv(3);
	C3D_TexEnvInit(env);
	if (minRepeat == maxRepeat)
		C3D_TexEnvColor(env, video_get_colour(1, vb_state->tVIPREG.BRTA * maxRepeat));
	C3D_TexEnvSrc(env, C3D_RGB, minRepeat == maxRepeat ? GPU_CONSTANT : GPU_TEXTURE2, GPU_PREVIOUS_BUFFER, GPU_PREVIOUS);
	C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_R);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);

	env = C3D_GetTexEnv(4);
	C3D_TexEnvInit(env);
	if (minRepeat == maxRepeat)
		C3D_TexEnvColor(env, video_get_colour(2, vb_state->tVIPREG.BRTB * maxRepeat));
	C3D_TexEnvSrc(env, C3D_RGB, minRepeat == maxRepeat ? GPU_CONSTANT : GPU_TEXTURE3, GPU_PREVIOUS, GPU_PREVIOUS_BUFFER);
	C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_G);
	C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);

	env = C3D_GetTexEnv(5);
	C3D_TexEnvInit(env);
	if (minRepeat == maxRepeat)
		C3D_TexEnvColor(env, video_get_colour(3, (vb_state->tVIPREG.BRTA + vb_state->tVIPREG.BRTB + vb_state->tVIPREG.BRTC) * maxRepeat));
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

	int dst_eye_count = default_for_both ? 2 : eye_count;

	for (int dst_eye = 0; dst_eye < dst_eye_count; dst_eye++) {
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
}

static void video_flush_soft(bool default_for_both, int displayed_fb) {
	u32 *inbuf[2] = {
		(u32*)(vb_state->V810_DISPLAY_RAM.off + 0x8000 * displayed_fb),
		(u32*)(vb_state->V810_DISPLAY_RAM.off + 0x8000 * displayed_fb + 0x10000),
	};
	u32 *outbuf[2] = {
		(u32*)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL),
		(u32*)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL),
	};

	u32 colors[4] = {
		__builtin_bswap32(video_get_colour(0, 0)),
		__builtin_bswap32(video_get_colour(1, vb_state->tVIPREG.BRTA * maxRepeat)),
		__builtin_bswap32(video_get_colour(2, vb_state->tVIPREG.BRTB * maxRepeat)),
		__builtin_bswap32(video_get_colour(3, (vb_state->tVIPREG.BRTA + vb_state->tVIPREG.BRTB + vb_state->tVIPREG.BRTC) * maxRepeat)),
	};

	bool use_column_table = minRepeat != maxRepeat;
	
	int viewportX = (TOP_SCREEN_WIDTH - VIEWPORT_WIDTH) / 2;
	int viewportY = (TOP_SCREEN_HEIGHT - VIEWPORT_HEIGHT) / 2;

	size_t columnJump = (TOP_SCREEN_HEIGHT - VIEWPORT_HEIGHT);

	for (int dst_eye = 0; dst_eye < (default_for_both ? 1 : eye_count); dst_eye++) {
		int src_eye = default_for_both ? orig_eye : !tVBOpt.ANAGLYPH && CONFIG_3D_SLIDERSTATE == 0 ? tVBOpt.DEFAULT_EYE : dst_eye;
		int depth_offset = (int)-getDepthOffset(default_for_both, dst_eye, tVBOpt.SLIDERMODE);
		memset(outbuf[dst_eye], 0, (viewportX + depth_offset) * 240 * 4);
		u32 *outbuf_end = outbuf[dst_eye] + (400 * 240);
		outbuf[dst_eye] += (viewportX + depth_offset) * (240);
		outbuf[dst_eye] += viewportY;

		video_soft_to_fb(outbuf[dst_eye], displayed_fb, src_eye, use_column_table, dst_eye == 1);

		outbuf[dst_eye] += VIEWPORT_WIDTH * TOP_SCREEN_HEIGHT;
		if (outbuf[dst_eye] < outbuf_end) {
			memset(outbuf[dst_eye], 0, (outbuf_end - outbuf[dst_eye]) * 4);
		}
	}

	gfxScreenSwapBuffers(GFX_TOP, !default_for_both && eye_count == 2);
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
				int col_a = video_get_colour(1, vb_state->tVIPREG.BRTA * (1 + table[t * 512 + (255 - i) * 2]));
				int col_b = video_get_colour(2, vb_state->tVIPREG.BRTB * (1 + table[t * 512 + (255 - i) * 2]));
				int col_c = video_get_colour(3, (vb_state->tVIPREG.BRTA + vb_state->tVIPREG.BRTB + vb_state->tVIPREG.BRTC) * (1 + table[t * 512 + (255 - i) * 2]));

				u8 *px = &tex[(128*8*t+((((i+1) & ~0xf) << 3) | (((i+1) & 8) << 3) | (((i+1) & 4) << 2) | (((i+1) & 2) << 1) | ((i+1) & 1))) * 3];
				px[2] = (col_a) & 0xff;
				px[1] = (col_a >> 8) & 0xff;
				px[0] = (col_a >> 16);

				coltable_proctex[t*96+i+1] = col_b;
				coltable_vbuf[t*96+i] = col_c;

				columnTableSoft[t][i][0] = __builtin_bswap32(col_a);
				columnTableSoft[t][i][1] = __builtin_bswap32(col_b);
				columnTableSoft[t][i][2] = __builtin_bswap32(col_c);
			}
		}
		// 1 pixel from the leftmost entry still leaks in
		coltable_proctex[0] = coltable_proctex[1];
		ProcTexColorLut_Write(&procTexColorLut, coltable_proctex, 0, 96*2+1);
	}
}

void gpu_blend_antiflicker(void) {
    C3D_BlendingColor(0x80808080);
    C3D_AlphaBlend(GPU_BLEND_ADD, 0, GPU_CONSTANT_ALPHA, GPU_ONE_MINUS_CONSTANT_ALPHA, 0, 0);
}

void gpu_blend_default(void) {
    C3D_ColorLogicOp(GPU_LOGICOP_COPY);
}

bool gpu_antiflicker_allowed(void) {
	// soft flush is incompatible with antiflicker
	return !USE_SOFT_FLUSH || (tVBOpt.RENDERMODE != RM_CPUONLY && tVBOpt.RENDERMODE != RM_TOCPU);
}

void gpu_flush(bool default_for_both, int displayed_fb, int vip_displayed_fb) {
	if (!default_for_both) orig_eye = tVBOpt.DEFAULT_EYE;
	if (eye_count == 2) default_for_both = false;

	if (tDSPCACHE.ColumnTableInvalid || (minRepeat != maxRepeat && tDSPCACHE.BrtPALMod))
		processColumnTable();

	if (!USE_SOFT_FLUSH || (tVBOpt.RENDERMODE != RM_CPUONLY && tVBOpt.RENDERMODE != RM_TOCPU))
		video_flush_hard(default_for_both, displayed_fb, vip_displayed_fb);
	else
		video_flush_soft(default_for_both, displayed_fb);
}

void gpu_quit() {
	C3D_Fini();
}
