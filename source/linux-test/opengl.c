#include <GLES2/gl2.h>

#include "video_hard.h"
#include "vb_dsp.h"
#include "v810_mem.h"

static GLuint tileTexture;
static u16 *tileTextureBuffer;
GLuint screenTexHard[2];
GLuint screenTargetHard[2];

extern GLuint sChar, sFinal;

static float palettes[8][3][3];

void gpu_init(void) {
    glGenTextures(1, &tileTexture);
    glBindTexture(GL_TEXTURE_2D, tileTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 512, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, NULL);
    tileTextureBuffer = malloc(256 * 512 * 2);

    glGenTextures(2, screenTexHard);
    glGenFramebuffers(2, screenTargetHard);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, screenTexHard[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 512, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, NULL);
        glBindFramebuffer(GL_FRAMEBUFFER, screenTargetHard[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, screenTexHard[i], 0);
    }

    for (int i = 0; i < AFFINE_CACHE_SIZE; i++) {
        glGenTextures(1, &tileMapCache[i].tex);
        glBindTexture(GL_TEXTURE_2D, tileMapCache[i].tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 512, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, NULL);
        glGenFramebuffers(1, &tileMapCache[i].target);
        glBindFramebuffer(GL_FRAMEBUFFER, tileMapCache[i].target);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tileMapCache[i].tex, 0);
    }

    glDepthMask(GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void gpu_clear_screen(int start_eye, int end_eye) {
    glClearColor(vb_state->tVIPREG.BKCOL == 1, vb_state->tVIPREG.BKCOL == 2, vb_state->tVIPREG.BKCOL == 3, vb_state->tVIPREG.BKCOL != 0);
    glClear(GL_COLOR_BUFFER_BIT);
}

void gpu_setup_drawing(void) {
    for (int i = 0; i < 4; i++) {
        const float colors[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        HWORD pal = vb_state->tVIPREG.GPLT[i];
        for (int j = 0; j < 3; j++) {
            pal = pal >> 2;
            memcpy(palettes[i][j], colors[pal & 3], sizeof(colors[0]));
        }
        pal = vb_state->tVIPREG.JPLT[i];
        for (int j = 0; j < 3; j++) {
            pal = pal >> 2;
            memcpy(palettes[i + 4][j], colors[pal & 3], sizeof(colors[0]));
        }
    }
}

void gpu_setup_tile_drawing(void) {
    glUseProgram(sChar);

    glEnableVertexAttribArray(glGetAttribLocation(sChar, "aPosition"));
    glVertexAttribPointer(glGetAttribLocation(sChar, "aPosition"), 2, GL_SHORT, GL_FALSE, sizeof(vertex), vbuf);
    glEnableVertexAttribArray(glGetAttribLocation(sChar, "aParams"));
    glVertexAttribPointer(glGetAttribLocation(sChar, "aParams"), 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(vertex), (u8*)vbuf + 4);
    glUniformMatrix3fv(glGetUniformLocation(sChar, "uPalette"), 8, GL_FALSE, &palettes[0][0][0]);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tileTexture);
    glUniform1i(glGetUniformLocation(sChar, "sTex"), 0);
}

void gpu_set_tile_offset(float xoffset, float yoffset) {
    glUniform2f(glGetUniformLocation(sChar, "uOffset"), xoffset, yoffset);
}

void gpu_init_vip_download(int previous_transfer_count, int start_eye, int end_eye, int drawn_fb) {}

void gpu_set_target(Framebuffer target) {
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    // all targets used in drawing are 512x512 so we can just set this here
    glViewport(0, 0, 512, 512);
}

void gpu_set_scissor(bool enabled, u32 left, u32 top, u32 right, u32 bottom) {
    if (enabled) glEnable(GL_SCISSOR_TEST);
    else glDisable(GL_SCISSOR_TEST);
    glScissor(left, top, right - left, bottom - top);
}

void gpu_set_opaque(bool opaque) {
    if (opaque) glDisable(GL_BLEND);
    else glEnable(GL_BLEND);
}

void gpu_draw_tiles(int first, int count) {
    glDrawArrays(GL_POINTS, first, count);
}

void update_texture_cache_hard(void) {
    for (int t = 0; t < 2048; t++) {
		// skip if this tile wasn't modified
		if (!tDSPCACHE.CharacterCache[t]) {
			if (blankTile < 0 && !tileVisible[t])
				blankTile = t;
			continue;
		}

        u16 *tile = (u16*)(vb_state->V810_DISPLAY_RAM.off + ((t & 0x600) << 6) + 0x6000 + (t & 0x1ff) * 16);

		int y = t / 32;
		int x = t % 32;
        u16 *dstbuf = tileTextureBuffer + y * 256 * 8 + x * 8;

        // optimize invisible tiles
		{
			bool tv = ((uint64_t*)tile)[0] | ((uint64_t*)tile)[1];
			tileVisible[t] = tv;
			if (!tv) {
				if (blankTile < 0) {
					blankTile = t;
				}
                for (int y = 0; y < 8; y++) {
                    memset(dstbuf + 256 * y, 0, 8 * 2);
                }
				continue;
			}
		}

        for (int y = 0; y < 8; y++) {
			// black, red, green, blue
			const static uint16_t colors[4] = {0, 0xf00f, 0x0f0f, 0x00ff};
            u16 row = *tile++;

            for (int x = 0; x < 8; x++) {
                *dstbuf++ = colors[row & 3];
                row >>= 2;
            }
            dstbuf -= 8;
            dstbuf += 256;
        }
    }
    glBindTexture(GL_TEXTURE_2D, tileTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 512, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, tileTextureBuffer);
}
