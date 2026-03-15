#include <GLES2/gl2.h>

#include "video_hard.h"
#include "vb_dsp.h"
#include "v810_mem.h"
#include "vb_set.h"

GLuint transparentPixelTexture;
static GLuint tileTexture;
static u16 *tileTextureBuffer;
GLuint screenTexHard[2];
GLuint screenTargetHard[2];
GLuint screenTexSoft[2];
static u16 *screenTexSoftBuffer;

GLuint sChar, sFinal, sAffine;

static float palettes[8][3][3];

static GLuint build_shader(const char *vertex_source, const char *fragment_source) {
    GLint compiled, infoLen;

    GLuint vshader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vshader, 1, &vertex_source, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        puts("Vertex shader failed to compile!");
        glGetShaderiv(vshader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            char *infoLog = malloc(infoLen);
            glGetShaderInfoLog(vshader, infoLen, NULL, infoLog);
            puts(infoLog);
            free(infoLog);
        }
    }
    
    GLuint fshader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fshader, 1, &fragment_source, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        puts("Fragment shader failed to compile!");
        glGetShaderiv(fshader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            char *infoLog = malloc(infoLen);
            glGetShaderInfoLog(fshader, infoLen, NULL, infoLog);
            puts(infoLog);
            free(infoLog);
        }
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vshader);
    glAttachShader(program, fshader);
    glLinkProgram(program);
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        puts("Shader program failed to link!");
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 0) {
            char *infoLog = malloc(infoLen);
            glGetProgramInfoLog(program, infoLen, NULL, infoLog);
            puts(infoLog);
            free(infoLog);
        }
    }

    return program;
}

void gpu_init(void) {
    sChar = build_shader(
        "uniform vec2 uOffset;\n"
        "uniform mat3 uPalette[8];\n"
        "attribute vec2 aPosition;\n"
        "attribute vec4 aParams;\n"
        "varying vec3 vParams;\n"
        "varying mat3 vPalette;\n"
        "void main() {\n"
        "   vParams = aParams.xyw;\n"
        "   vPalette = uPalette[int(aParams.z)];\n"
        "   vec2 realPos = aPosition * (1.0/256.0) - (1.0 - 4.0/256.0) + uOffset;\n"
        "   gl_Position = vec4(realPos.y, realPos.x, 0.0, 1.0);\n"
        "   gl_PointSize = 8.0;\n"
        "}\n",

        "uniform sampler2D sTex;\n"
        "varying mediump vec3 vParams;\n"
        "varying mediump mat3 vPalette;\n"
        "void main() {\n"\
        "   mediump vec2 tileCoord = gl_PointCoord.yx;\n"
        "   if (vParams.z >= 2.0) tileCoord.x = 1.0 - tileCoord.x;\n"
        "   if (vParams.z == 1.0 || vParams.z == 3.0) tileCoord.y = 1.0 - tileCoord.y;\n"
        "   mediump vec2 realCoord = (vParams.xy + vec2(1.0 - tileCoord.x, tileCoord.y)) / vec2(256.0 / 8.0, 512.0 / 8.0);\n"
        "   lowp vec4 color = texture2D(sTex, realCoord);\n"
        "   gl_FragColor = vec4(vPalette * color.xyz, color.w);\n"
        "}\n"
    );

    sAffine = build_shader(
        "attribute vec4 aParams;\n"
        "attribute vec2 aOffset;\n"
        "varying vec2 vTexCoord;\n"
        "void main() {\n"
        "   gl_Position = vec4(aParams.yx / 256.0 - 1.0, 0.0, 1.0);\n"
        "   vTexCoord = (aParams.zw + aOffset) / 4096.0;\n"
        "}\n",

        "uniform sampler2D sTex;\n"
        "uniform bool uRepeat;\n"
        "uniform highp float uStartMap;\n"
        "// (fullwidth, height, modwidth)\n"
        "uniform highp vec3 uWorldSize;\n"
        "uniform bool uMapVisible[8];\n"
        "varying highp vec2 vTexCoord;\n"
        "void main() {\n"
        "   highp vec2 texCoord = vTexCoord;\n"
        "   if (!uRepeat && mod(texCoord, uWorldSize.xy) != texCoord) {\n"
        "       // TODO: overtile\n"
        "       gl_FragColor = vec4(0.0);\n"
        "       return;\n"
        "   }\n"
        "   texCoord = mod(texCoord, uWorldSize.zy);\n"
        "   mediump float sub_map = floor(texCoord.x) + floor(texCoord.y) * uWorldSize.x;\n"
        "   if (!uMapVisible[int(sub_map)]) {\n"
        "       gl_FragColor = vec4(0.0);\n"
        "       return;\n"
        "   }\n"
        "   highp vec2 mapOffset = vec2(mod(floor(uStartMap + sub_map), 4.0), floor((uStartMap + sub_map) / 4.0));\n"
        "   gl_FragColor = texture2D(sTex, (mod(texCoord.yx, 1.0) + mapOffset) * vec2(1.0 / 4.0, 1.0 / 2.0));\n"
        "}\n"
    );

    sFinal = build_shader(
        "attribute vec4 aPosition;\n"
        "attribute vec2 aTexCoord;\n"
        "varying vec2 vTexCoord;\n"
        "void main() {\n"
        "   gl_Position = aPosition;\n"
        "   vTexCoord = aTexCoord;\n"
        "}\n",

        "uniform sampler2D sVip, sSoft;\n"
        "uniform mediump vec3 uPalette[4];\n"
        "varying mediump vec2 vTexCoord;\n"
        "void main() {\n"
        "   mediump vec4 vip = texture2D(sVip, vTexCoord);\n"
        "   mediump vec4 soft = texture2D(sSoft, vTexCoord);\n"
        "   mediump vec4 color = soft.a == 0.0 ? vip : soft;\n"
        "   gl_FragColor = vec4(mix(mix(mix(uPalette[0], uPalette[1], color.x), uPalette[2], color.y), uPalette[3], color.z), 1.0);\n"
        "}\n"
    );

    glGenTextures(1, &transparentPixelTexture);
    glBindTexture(GL_TEXTURE_2D, transparentPixelTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    u16 pixel = 0;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, &pixel);
    

    glGenTextures(1, &tileTexture);
    glBindTexture(GL_TEXTURE_2D, tileTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 512, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, NULL);
    tileTextureBuffer = malloc(256 * 512 * sizeof(tileTextureBuffer[0]));

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

    glGenTextures(2, screenTexSoft);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, screenTexSoft[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 512, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, NULL);
    }
    screenTexSoftBuffer = malloc(512 * 512 * sizeof(screenTexSoftBuffer[0]));

    glGenTextures(1, &tileMapCache[0].tex);
    glGenFramebuffers(1, &tileMapCache[0].target);
    for (int i = 1; i < AFFINE_CACHE_SIZE; i++) {
        tileMapCache[i].tex = tileMapCache[0].tex;
        tileMapCache[i].target = tileMapCache[0].target;
    }
    glBindTexture(GL_TEXTURE_2D, tileMapCache[0].tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512*4, 512*2, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, tileMapCache[0].target);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tileMapCache[0].tex, 0);

    glDepthMask(GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void gpu_clear_screen(int start_eye, int end_eye) {
    glClearColor(vb_state->tVIPREG.BKCOL == 1, vb_state->tVIPREG.BKCOL == 2, vb_state->tVIPREG.BKCOL == 3, vb_state->tVIPREG.BKCOL != 0);
    glClear(GL_COLOR_BUFFER_BIT);
}

void gpu_setup_drawing(void) {
    for (int i = 0; i < 4; i++) {
        const float normal_cols[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        const float tocpu_cols[4][3] = {{0, 0, 0}, {0, 1.0/16, 0}, {0, 2.0/16}, {0, 3.0/16, 0}};
        const float (*cols)[3] = (tVBOpt.RENDERMODE != RM_TOCPU) ? normal_cols : tocpu_cols;
        HWORD pal = vb_state->tVIPREG.GPLT[i];
        for (int j = 0; j < 3; j++) {
            pal = pal >> 2;
            memcpy(palettes[i][j], cols[pal & 3], sizeof(cols[0]));
        }
        pal = vb_state->tVIPREG.JPLT[i];
        for (int j = 0; j < 3; j++) {
            pal = pal >> 2;
            memcpy(palettes[i + 4][j], cols[pal & 3], sizeof(cols[0]));
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

void gpu_target_screen(int drawn_fb) {
    glBindFramebuffer(GL_FRAMEBUFFER, screenTargetHard[drawn_fb]);
    glViewport(0, 0, 512, 512);
}

void gpu_target_affine(int cache_id) {
    glBindFramebuffer(GL_FRAMEBUFFER, tileMapCache[cache_id].target);
    int xoff = cache_id % 4;
    int yoff = cache_id / 4;
    glViewport(512 * xoff, 512 * yoff, 512, 512);
    gpu_set_scissor(true, 512 * xoff, 512 * yoff, 512 * (xoff + 1), 512 * (yoff + 1));
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

    glUseProgram(sAffine);

    glEnableVertexAttribArray(glGetAttribLocation(sAffine, "aParams"));
    glVertexAttribPointer(glGetAttribLocation(sAffine, "aParams"), 4, GL_SHORT, GL_FALSE, sizeof(avertex) / 2, avbuf);
    glEnableVertexAttribArray(glGetAttribLocation(sAffine, "aOffset"));
    glVertexAttribPointer(glGetAttribLocation(sAffine, "aOffset"), 2, GL_SHORT, GL_FALSE, sizeof(avertex) / 2, (u8*)avbuf + 8);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tileMapCache[0].tex);
    glUniform1i(glGetUniformLocation(sAffine, "sTex"), 0);
    glUniform1i(glGetUniformLocation(sAffine, "uRepeat"), !over);
    glUniform1f(glGetUniformLocation(sAffine, "uStartMap"), mapid % AFFINE_CACHE_SIZE);
    glUniform3f(glGetUniformLocation(sAffine, "uWorldSize"), scx, scy, huge_bg ? 8 / scy : scx);
    GLint uVisible[8];
    for (int sub_bg = 0; sub_bg < map_count; sub_bg++) {
		int cache_id = (mapid + sub_bg) % AFFINE_CACHE_SIZE;
        uVisible[sub_bg] = visible[sub_bg] && tileMapCache[cache_id].bg == mapid + sub_bg && tileMapCache[cache_id].visible;
    }
    glUniform1iv(glGetUniformLocation(sAffine, "uMapVisible"), map_count, uVisible);

    for (int eye = 0; eye < 2; eye++) {
        if (vbufs[eye] != NULL) {
            int gx = base_gx + (eye == 0 ? -gp : gp);
            
            // note: transposed
            gpu_set_scissor(true, 256 * eye + (gy >= 0 ? gy : 0), gx >= 0 ? gx : 0, (gy + h < 256 ? gy + h : 256) + 256 * eye, gx + w);
            glDrawArrays(GL_LINES, (vbufs[eye] - avbuf) * 2, h * 2);
        }
    }
}

void update_texture_cache_hard(void) {
    int min_updated = 2048, max_updated = -1;
    for (int t = 0; t < 2048; t++) {
		// skip if this tile wasn't modified
		if (!tDSPCACHE.CharacterCache[t]) {
			if (blankTile < 0 && !tileVisible[t])
				blankTile = t;
			continue;
		}

        if (min_updated > t) min_updated = t;
        if (max_updated < t) max_updated = t;

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
    if (min_updated <= max_updated) {
        glBindTexture(GL_TEXTURE_2D, tileTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, (min_updated / 32) * 8, 256, (max_updated / 32 + 1 - min_updated / 32) * 8, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, tileTextureBuffer + (min_updated / 32 * 256 * 8));
    }
}

void video_soft_to_texture(int displayed_fb) {
    if (tDSPCACHE.DDSPDataState[displayed_fb] == CPU_WROTE) {
        tDSPCACHE.DDSPDataState[displayed_fb] = GPU_WROTE;
    } else {
        return;
    }
    int start_eye = eye_count == 2 ? 0 : tVBOpt.DEFAULT_EYE;
    for (int eye = start_eye; eye < start_eye + eye_count; eye++) {
        for (int x = 0; x < 384; x++) {
            uint16_t *in_fb_ptr = (uint16_t*)(vb_state->V810_DISPLAY_RAM.off + 0x10000 * eye + 0x8000 * displayed_fb + x * (256 / 4));
            uint16_t *out_fb = screenTexSoftBuffer + x * 512 + eye * 256;

            for (int y = 0; y < 224 / 8; y++) {
                // black, red, green, blue
                const static uint16_t colors[4] = {0, 0xf00f, 0x0f0f, 0x00ff};

                uint16_t blob = *in_fb_ptr++;
                for (int i = 0; i < 16 / 2; i++) {
                    *out_fb++ = colors[blob & 3];
                    blob >>= 2;
                }
            }
        }
    }
    glBindTexture(GL_TEXTURE_2D, screenTexSoft[displayed_fb]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 512, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, screenTexSoftBuffer);
}


static bool downloaded;

void gpu_init_vip_download(int previous_transfer_count, int start_eye, int end_eye, int drawn_fb) {
    downloaded = false;
}

void video_download_vip(int drawn_fb) {
	if (tVBOpt.RENDERMODE != RM_TOCPU) return;
	if (downloaded) return;
    downloaded = true;
    glBindFramebuffer(GL_FRAMEBUFFER, screenTargetHard[drawn_fb]);
    for (int eye = 0; eye < 2; eye++) {
        glReadPixels(eye * 256, 0, DOWNLOADED_FRAMEBUFFER_WIDTH, 384, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, rgba4_framebuffers);
        uint32_t *in_fb = (uint32_t*)rgba4_framebuffers;
		uint32_t *out_fb = (uint32_t*)(vb_state->V810_DISPLAY_RAM.off + 0x10000 * eye + 0x8000 * drawn_fb);
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
    }

	tDSPCACHE.DDSPDataState[drawn_fb] = CPU_WROTE;
	for (int i = 0; i < 64; i++) {
		tDSPCACHE.SoftBufWrote[drawn_fb][i].min = 0;
		tDSPCACHE.SoftBufWrote[drawn_fb][i].max = 31;
	}
}
