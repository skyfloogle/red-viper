#include "vb_dsp.h"
#include "video_hard.h"
#include "v810_mem.h"
#include "vb_set.h"

VB_DSPCACHE tDSPCACHE; // Array of Display Cache info...

int eye_count = 2;

void video_init(bool also_gpu) {
    if (also_gpu) video_hard_init();
	setup_brightness_lut();
}

static int g_displayed_fb = 0;
static int vip_displayed_fb = 0;

void video_render(int displayed_fb, bool on_time) {
    gpu_reset_vip_download();

	bool antiflicker = gpu_antiflicker_allowed() && tVBOpt.ANTIFLICKER && on_time;

	static HWORD old_brt[3];
	HWORD new_brt[3] = {vb_state->tVIPREG.BRTA, vb_state->tVIPREG.BRTB, vb_state->tVIPREG.BRTC};
	if (antiflicker) {
		vb_state->tVIPREG.BRTA = old_brt[0];
		vb_state->tVIPREG.BRTB = old_brt[1];
		vb_state->tVIPREG.BRTC = old_brt[2];
		video_flush(false);
		vb_state->tVIPREG.BRTA = new_brt[0];
		vb_state->tVIPREG.BRTB = new_brt[1];
		vb_state->tVIPREG.BRTC = new_brt[2];
	}
	memcpy(old_brt, new_brt, sizeof(new_brt));

	// Red Alarm mostly benefits from the VIP being drawn over the software buffer.
	// The exception is the controls menu, which needs the lines on top.
	// The controls menu is the only place in the game where world 12 is used,
	// so if world 12 is enabled, turn off VIP_OVER_SOFT.
	if (
        CHECK_GAMEID("01VREE") // Red Alarm (U)
        || CHECK_GAMEID("E4VREJ") // Red Alarm (J)
    ) {
        tVBOpt.VIP_OVER_SOFT = ((WORLD *)(vb_state->V810_DISPLAY_RAM.off + 0x3d800))[12].head == 0;
    }

	// Hacks that change render mode depending on game. Requires GPU.
	if (tVBOpt.GPU_AVAILABLE) {
    	// Golf hack: switch to software rendering during gameplay.
        if (CHECK_GAMEID("01VVGE") || CHECK_GAMEID("E4VVGJ")) {
            if (*(uint8_t*)(vb_state->V810_DISPLAY_RAM.off + 0x3dbc0) == 0x40 &&
                *(uint16_t*)(vb_state->V810_DISPLAY_RAM.off + 0x3dbe6) == 0x48 &&
                memcmp((uint8_t*)vb_state->V810_DISPLAY_RAM.off + 0x3dbec, "\0\0\x80\x01\x1f\0\0\x80\0\0", 10) == 0
            ) {
                // looks like hills, do software rendering
                if (tVBOpt.RENDERMODE != RM_CPUONLY) {
                    tVBOpt.RENDERMODE = RM_CPUONLY;
                    clearCache();
                }
            } else {
                // switch back to hardware rendering
                if (tVBOpt.RENDERMODE != RM_TOGPU) {
                    tVBOpt.RENDERMODE = RM_TOGPU;
                    for (int i = 0; i < 3; i++) {
                        memset((uint8_t*)vb_state->V810_DISPLAY_RAM.off + (0x8000 * i), 0, 0x6000);
                    }
                }
            }
        }
        // Test Chamber hack: switch to VIP downloading during gameplay.
        // Otherwise, the intro and ending will slow down.
        if (CHECK_GAMEID("PRCHMB")) {
            WORLD *worlds = (WORLD *)(vb_state->V810_DISPLAY_RAM.off + 0x3d800);
            // Spot that we're in the emulator tester.
            bool vip_download = worlds[30].end;
            if (!vip_download) {
                // We assume we're in gameplay if there are affine worlds.
                for (int i = 31; i >= 0; i--) {
                    if (worlds[i].end)
                        break;
                    if (worlds[i].on == 0)
                        continue;
                    if (worlds[i].bgm == 2) {
                        vip_download = true;
                        break;
                    }
                }
            }
            if (vip_download) {
                tVBOpt.RENDERMODE = RM_TOCPU;
            } else {
                if (tVBOpt.RENDERMODE != RM_TOGPU) {
                    tVBOpt.RENDERMODE = RM_TOGPU;
                    gpu_clear_screen(false);
                }
            }
        }
	} else {
	    tVBOpt.RENDERMODE = RM_CPUONLY;
	}

	g_displayed_fb = displayed_fb;
	vip_displayed_fb = tVBOpt.DOUBLE_BUFFER ? displayed_fb : 0;

	bool should_flush = antiflicker || (vb_state->tVIPREG.XPCTRL & XPEN) || tDSPCACHE.DDSPDataState[displayed_fb] == CPU_WROTE || tDSPCACHE.ColumnTableInvalid || tDSPCACHE.BrtPALMod;

	if (tVBOpt.GPU_AVAILABLE && (tVBOpt.RENDERMODE == RM_TOGPU || tVBOpt.RENDERMODE == RM_TOCPU || tVBOpt.RENDERMODE == RM_CPUONLY)) {
		// postproc (can be done early)
		gpu_soft_to_texture(displayed_fb);
	}

	if (tVBOpt.DOUBLE_BUFFER) {
		if (antiflicker) gpu_blend_antiflicker();
		if (should_flush) {
			video_flush(false);
		}
		if (antiflicker) gpu_blend_default();
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
		#ifdef NEED_BG_CACHE
		tDSPCACHE.BGCacheInvalid = 0;
		#endif
		memset(tDSPCACHE.CharacterCache, 0, sizeof(tDSPCACHE.CharacterCache));
	}

    if (!tVBOpt.DOUBLE_BUFFER) {
		if (antiflicker) gpu_blend_antiflicker();
		if (should_flush) {
			video_flush(false);
		}
		if (antiflicker) gpu_blend_default();
    }
}

void video_flush(bool default_for_both) {
    gpu_flush(default_for_both, g_displayed_fb, vip_displayed_fb);

	// cleanup
	tDSPCACHE.ColumnTableInvalid = false;
	tDSPCACHE.BrtPALMod = false;
}

void video_quit(void) {
	if (tVBOpt.GPU_AVAILABLE) video_hard_quit();
}
