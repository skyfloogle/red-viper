#include "vb_dsp.h"
#include "vb_set.h"
#include "v810_mem.h"

C3D_Tex screenTexSoft[2];

uint32_t columnTableSoft[2][96][3];

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
                uint16_t *in_fb_ptr = (uint16_t*)(vb_state->V810_DISPLAY_RAM.off + 0x10000 * eye + 0x8000 * displayed_fb + tx * (256 / 4 * 8) + ty * 2);
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
    if (CHECK_GAMEID("PRCHMB") && tVBOpt.RENDERMODE != RM_CPUONLY) {
        video_soft_to_texture_inner<true>(displayed_fb);
    } else {
        video_soft_to_texture_inner<false>(displayed_fb);
    }
}

template<bool anaglyph, bool additive> void video_soft_to_fb_inner(u32 *outbuf, int displayed_fb, int src_eye, bool use_column_table) {
	u32 *inbuf = (u32*)(vb_state->V810_DISPLAY_RAM.off + 0x8000 * displayed_fb + 0x10000 * src_eye);

    const u32 anaglyph_colors[8] = {0, 0xff000000, 0x00ff0000, 0xffff0000, 0x0000ff00, 0xff00ff00, 0x00ffff00, 0xffffff00};
    u32 anaglyph_tint = anaglyph ? anaglyph_colors[src_eye == 0 ? tVBOpt.ANAGLYPH_LEFT : tVBOpt.ANAGLYPH_RIGHT] : UINT32_MAX;

	u32 colors[4] = {
		__builtin_bswap32(video_get_colour(0, 0)) & anaglyph_tint,
		__builtin_bswap32(video_get_colour(1, vb_state->tVIPREG.BRTA * maxRepeat)) & anaglyph_tint,
		__builtin_bswap32(video_get_colour(2, vb_state->tVIPREG.BRTB * maxRepeat)) & anaglyph_tint,
		__builtin_bswap32(video_get_colour(3, (vb_state->tVIPREG.BRTA + vb_state->tVIPREG.BRTB + vb_state->tVIPREG.BRTC) * maxRepeat)) & anaglyph_tint,
	};
		
    // we step the column backwards, so skip to end
    outbuf += 224;

    for (int x = 0; x < 384; x++) {
        if (use_column_table && unlikely(x % 4 == 0)) {
            colors[1] = columnTableSoft[src_eye][x / 4][0] & anaglyph_tint;
            colors[2] = columnTableSoft[src_eye][x / 4][1] & anaglyph_tint;
            colors[3] = columnTableSoft[src_eye][x / 4][2] & anaglyph_tint;
        }
        for (int ty = 0; ty < 224 / 16; ty++) {
            u32 intile = *inbuf++;
            for (int p = 0; p < 16; p++) {
                if (additive) *--outbuf |= colors[intile & 3];
                else          *--outbuf = colors[intile & 3];
                intile >>= 2;
            }
        }
        // inbuf is 256 tall = 32 extra pixels
        inbuf += 2;
        // skip from start of one to end of next
        outbuf += (240 + 224);
    }
}

void video_soft_to_fb(u32 *outbuf, int displayed_fb, int src_eye, bool use_column_table, bool additive) {
    if (tVBOpt.ANAGLYPH) {
        if (additive) video_soft_to_fb_inner<true, true>(outbuf, displayed_fb, src_eye, use_column_table);
        else video_soft_to_fb_inner<true, false>(outbuf, displayed_fb, src_eye, use_column_table);
    } else video_soft_to_fb_inner<false, false>(outbuf, displayed_fb, src_eye, use_column_table);
}
