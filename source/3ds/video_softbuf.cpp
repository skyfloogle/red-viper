#include "vb_dsp.h"
#include "vb_set.h"
#include "v810_mem.h"

C3D_Tex screenTexSoft[2];
uint32_t *screenTexSoftLinear[2];

void video_soft_init(void) {
	C3D_TexInitParams params;
	params.width = 512;
	params.height = 512;
	params.format = GPU_RGBA4;
	params.type = GPU_TEX_2D;
	params.onVram = false;
	params.maxLevel = 0;
	for (int i = 0; i < 2; i++) {
        C3D_TexInitWithParams(&screenTexSoft[i], NULL, params);
        screenTexSoftLinear[i] = (uint32_t*)linearAlloc(512 * 512 * 2);
    }
}

// using a template to avoid checking a boolean in a hot loop
template<bool copy_alpha> void video_soft_to_texture_inner(int displayed_fb) {
    uint32_t fb_size;
    uint32_t *out_fb = (uint32_t*)C3D_Tex2DGetImagePtr(&screenTexSoft[displayed_fb], 0, &fb_size);
    if (tDSPCACHE.DDSPDataState[displayed_fb] == CPU_WROTE) {
        tDSPCACHE.DDSPDataState[displayed_fb] = GPU_WROTE;
    } else {
        return;
    }
    // copy framebuffer
    int start_eye = eye_count == 2 ? 0 : tVBOpt.DEFAULT_EYE;
    for (int eye = start_eye; eye < start_eye + eye_count; eye++) {
        for (int x = 0; x < 384; x++) {
            int ymin, ymax;
            SOFTBOUND *column = &tDSPCACHE.SoftBufWrote[displayed_fb][x / 8];
            ymin = column->min;
            ymax = column->max;
            if (ymin > ymax) {
                continue;
            }
            if (++ymax > 224 / 8) ymax = 224 / 8;

            uint16_t *in_fb_ptr = (uint16_t*)(V810_DISPLAY_RAM.off + 0x10000 * eye + 0x8000 * displayed_fb + x * (256 / 4)) + ymin;
            uint16_t *in_alpha_ptr = &tDSPCACHE.OpaquePixels.u16[displayed_fb][eye][x * (256 / 4 / 2)] + ymin;
            uint16_t *out = (uint16_t*)&screenTexSoftLinear[displayed_fb][x * 256] + 256 * eye + ymin * 8;

            for (int ty = ymin; ty < ymax; ty++) {
                // black, red, green, blue
                const static uint16_t colors[4] = {0, 0xf00f, 0x0f0f, 0x00ff};
                uint32_t in_col = *in_fb_ptr++;
                uint32_t in_alpha = *in_alpha_ptr++;
                for (int i = 0; i < 8; i++) {
                    *out++ = colors[in_col & 3] | (copy_alpha ? 0xf * !!(in_alpha & 3) : 0);
                    in_col >>= 2;
                    in_alpha >>= 2;
                }
            }
        }
        GX_DisplayTransfer(screenTexSoftLinear[displayed_fb], GX_BUFFER_DIM(512, 512), out_fb, GX_BUFFER_DIM(512, 512),
             (GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
            GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA4) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA4) |
            GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))
        );
    }
    // reset column cache for any following writes
    for (int tx = 0; tx < 384 / 8; tx++) {
        SOFTBOUND *column = &tDSPCACHE.SoftBufWrote[displayed_fb][tx];
        column->min = 0xff;
        column->max = 0;
    }
}

void video_soft_to_texture(int displayed_fb) {
    if (memcmp(tVBOpt.GAME_ID, "PRCHMB", 6) == 0 && tVBOpt.RENDERMODE < 2) {
        video_soft_to_texture_inner<true>(displayed_fb);
    } else {
        video_soft_to_texture_inner<false>(displayed_fb);
    }
}