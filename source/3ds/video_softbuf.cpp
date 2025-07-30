#include "vb_dsp.h"
#include "vb_set.h"
#include "v810_mem.h"

C3D_Tex screenTexSoft[2];

void video_soft_init(void) {
	C3D_TexInitParams params;
	params.width = 512;
	params.height = 512;
	params.format = GPU_RGBA4;
	params.type = GPU_TEX_2D;
	params.onVram = false;
	params.maxLevel = 0;
	for (int i = 0; i < 2; i++)
        C3D_TexInitWithParams(&screenTexSoft[i], NULL, params);
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
        for (int tx = 0; tx < 384 / 8; tx++) {
            uint32_t *column_ptr = &out_fb[8 * 8 / 4 * 2 * (eye * 256 / 8 + 512 / 8 * (512 / 8 - 1 - tx))];
            int ymin, ymax;
            SOFTBOUND *column = &tDSPCACHE.SoftBufWrote[displayed_fb][tx];
            ymin = column->min;
            ymax = column->max;
            if (ymin > ymax) {
                continue;
            }
            if (++ymax > 224 / 8) ymax = 224 / 8;

            for (int ty = ymin; ty < ymax; ty++) {
                uint32_t *out_tile = &column_ptr[8 * 8 / 4 * 2 * (1 + ty)];
                uint16_t *in_fb_ptr = (uint16_t*)(V810_DISPLAY_RAM.pmemory + 0x10000 * eye + 0x8000 * displayed_fb + tx * (256 / 4 * 8) + ty * 2);
                uint16_t *in_alpha_ptr = &tDSPCACHE.OpaquePixels.u16[displayed_fb][eye][tx * (256 / 4 * 8) / 2 + ty];

                for (int i = 0; i <= 2; i += 2) {
                    uint32_t slice1, slice2, slice1_alpha, slice2_alpha;
                    slice2 = *in_fb_ptr;
                    in_fb_ptr += 256 / 4 / 2;
                    slice2 |= (*in_fb_ptr << 16);
                    in_fb_ptr += 256 / 4 / 2;
                    slice1 = *in_fb_ptr;
                    in_fb_ptr += 256 / 4 / 2;
                    slice1 |= (*in_fb_ptr << 16);
                    in_fb_ptr += 256 / 4 / 2;

                    if (copy_alpha) {
                        slice2_alpha = *in_alpha_ptr;
                        in_alpha_ptr += 256 / 4 / 2;
                        slice2_alpha |= (*in_alpha_ptr << 16);
                        in_alpha_ptr += 256 / 4 / 2;
                        slice1_alpha = *in_alpha_ptr;
                        in_alpha_ptr += 256 / 4 / 2;
                        slice1_alpha |= (*in_alpha_ptr << 16);
                        in_alpha_ptr += 256 / 4 / 2;
                    }
	
                    // black, red, green, blue
                    const static uint16_t colors[4] = {0, 0xf00f, 0x0f0f, 0x00ff};

                    #define SQUARE(x, i) { \
                        uint32_t left  = x >> (0 + 4*i) & 0x00030003; \
                        uint32_t right = x >> (2 + 4*i) & 0x00030003; \
                        uint32_t left_alpha = copy_alpha ? x##_alpha >> (0 + 4*i) & 0x00030003 : 0; \
                        uint32_t right_alpha = copy_alpha ? x##_alpha >> (2 + 4*i) & 0x00030003 : 0; \
                        *--out_tile = colors[(uint16_t)left] | (0xf * !!(uint16_t)left_alpha) | (colors[(uint16_t)right] << 16) | (0xf0000 * !!(uint16_t)right_alpha); \
                        *--out_tile = colors[left >> 16] | (0xf * !!(left_alpha >> 16)) | (colors[right >> 16] << 16) | (0xf0000 * !!(right_alpha >> 16)); \
                    }

                    SQUARE(slice2, 3);
                    SQUARE(slice2, 2);
                    SQUARE(slice1, 3);
                    SQUARE(slice1, 2);
                    SQUARE(slice2, 1);
                    SQUARE(slice2, 0);
                    SQUARE(slice1, 1);
                    SQUARE(slice1, 0);

                    #undef SQUARE
                }
            }
        }
    }
    // reset column cache for any following writes
    for (int tx = 0; tx < 384 / 8; tx++) {
        SOFTBOUND *column = &tDSPCACHE.SoftBufWrote[displayed_fb][tx];
        column->min = 0xff;
        column->max = 0;
    }
}

void video_soft_to_texture(int displayed_fb) {
    if (memcmp(tVBOpt.GAME_ID, "PRCHMB", 6) == 0) {
        video_soft_to_texture_inner<true>(displayed_fb);
    } else {
        video_soft_to_texture_inner<false>(displayed_fb);
    }
}