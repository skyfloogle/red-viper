#include "vb_dsp.h"
#include "video_hard.h"
#include "v810_mem.h"
#include "vb_set.h"

VB_DSPCACHE tDSPCACHE; // Array of Display Cache info...

int eye_count = 2;

void video_init(void) {
    video_hard_init();
	setup_brightness_lut();
}

static int g_displayed_fb = 0;
static int vip_displayed_fb = 0;

void video_render(int displayed_fb, bool on_time) {
	if (tVBOpt.RENDERMODE == RM_TOCPU) video_download_vip(displayed_fb);

    gpu_reset_vip_download();

    #ifdef __3DS__
	if (tVBOpt.ANTIFLICKER && on_time) video_flush(false);
    #endif

	g_displayed_fb = displayed_fb;
	vip_displayed_fb = tVBOpt.DOUBLE_BUFFER ? displayed_fb : 0;

	if (tVBOpt.RENDERMODE == RM_TOGPU || ((tVBOpt.RENDERMODE == RM_TOCPU || tVBOpt.RENDERMODE == RM_CPUONLY))) {
		// postproc (can be done early)
		gpu_soft_to_texture(displayed_fb);
	}

	if (tVBOpt.DOUBLE_BUFFER) {
        #ifdef __3DS__
		C3D_BlendingColor(0x80808080);
		if (tVBOpt.ANTIFLICKER && on_time) C3D_AlphaBlend(GPU_BLEND_ADD, 0, GPU_CONSTANT_ALPHA, GPU_ONE_MINUS_CONSTANT_ALPHA, 0, 0);
        #endif
		video_flush(false);
        #ifdef __3DS__
		C3D_ColorLogicOp(GPU_LOGICOP_COPY);
        #endif
	}

    #ifdef __3DS__
	C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
	AttrInfo_Init(attrInfo);
	AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 4);
	AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 4);
	AttrInfo_AddLoader(attrInfo, 2, GPU_UNSIGNED_BYTE, 4);

	eye_count = tVBOpt.ANAGLYPH || tVBOpt.RENDERMODE == RM_TOCPU || CONFIG_3D_SLIDERSTATE > 0.0f ? 2 : 1;
    #endif

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
			video_soft_render(!displayed_fb);
		}

		// we need to have these caches during rendering
		tDSPCACHE.CharCacheInvalid = false;
		memset(tDSPCACHE.BGCacheInvalid, 0, sizeof(tDSPCACHE.BGCacheInvalid));
		memset(tDSPCACHE.CharacterCache, 0, sizeof(tDSPCACHE.CharacterCache));
	}

    if (!tVBOpt.DOUBLE_BUFFER) {
        #ifdef __3DS__
		C3D_BlendingColor(0x80808080);
		if (tVBOpt.ANTIFLICKER && on_time) C3D_AlphaBlend(GPU_BLEND_ADD, 0, GPU_CONSTANT_ALPHA, GPU_ONE_MINUS_CONSTANT_ALPHA, 0, 0);
        #endif
        video_flush(false);
        #ifdef __3DS__
		C3D_ColorLogicOp(GPU_LOGICOP_COPY);
        #endif
    }
}

void video_flush(bool default_for_both) {
    gpu_flush(default_for_both, g_displayed_fb, vip_displayed_fb);

	// cleanup
	tDSPCACHE.ColumnTableInvalid = false;
	tDSPCACHE.BrtPALMod = false;
}

void video_quit(void) {
	gpu_quit();
}
