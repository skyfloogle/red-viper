#include "GLES2/gl2.h"

#include "vb_dsp.h"
#include "video_hard.h"
#include "v810_mem.h"
#include "vb_set.h"

extern GLuint sFinal;

extern GLuint transparentPixelTexture;
extern GLuint screenTexSoft[2];

int eye_count = 2;

void video_hard_init(void);

void video_init(void) {
    video_hard_init();
}

extern GLuint screenTexHard[2];
void video_hard_render(int drawn_fb);
void video_soft_to_texture(int displayed_fb);

static int g_displayed_fb = 0;
static int vip_displayed_fb = 0;

void video_render(int displayed_fb, bool on_time) {
	g_displayed_fb = displayed_fb;
	vip_displayed_fb = tVBOpt.DOUBLE_BUFFER ? displayed_fb : 0;

	if (tVBOpt.RENDERMODE == RM_TOGPU || ((tVBOpt.RENDERMODE == RM_TOCPU || tVBOpt.RENDERMODE == RM_CPUONLY))) {
		// postproc (can be done early)
		video_soft_to_texture(displayed_fb);
	}

	if (tVBOpt.DOUBLE_BUFFER) {
		video_flush(displayed_fb);
	}

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
            gpu_target_screen(!vip_displayed_fb);
            glClearColor(0.0, 0.0, 0.0, 0.0);
            glClear(GL_COLOR_BUFFER_BIT);
			video_soft_render(!displayed_fb);
		}

		// we need to have these caches during rendering
		tDSPCACHE.CharCacheInvalid = false;
		memset(tDSPCACHE.BGCacheInvalid, 0, sizeof(tDSPCACHE.BGCacheInvalid));
		memset(tDSPCACHE.CharacterCache, 0, sizeof(tDSPCACHE.CharacterCache));
	}

    video_flush(displayed_fb);
}

void video_flush(bool left_for_both) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, 384*2, 224*2);
    glScissor(0, 0, 384*2, 224*2);
    glUseProgram(sFinal);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, screenTexHard[vip_displayed_fb]);
    glUniform1i(glGetUniformLocation(sFinal, "sVip"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tDSPCACHE.DDSPDataState[g_displayed_fb] != GPU_CLEAR ? screenTexSoft[g_displayed_fb] : transparentPixelTexture);
    glUniform1i(glGetUniformLocation(sFinal, "sSoft"), 1);
    
    float colors[4][3] = {
        {0, 0, 0},
        {vb_state->tVIPREG.BRTA / 128.0, 0, 0},
        {vb_state->tVIPREG.BRTB / 128.0, 0, 0},
        {(vb_state->tVIPREG.BRTA + vb_state->tVIPREG.BRTB + vb_state->tVIPREG.BRTC) / 128.0, 0, 0},
    };
    glUniform3fv(glGetUniformLocation(sFinal, "uPalette"), 4, &colors[0][0]);

    GLfloat vPositions[] = {
        -1, -1,
        1, -1,
        -1, 1,
        1, 1};
    glVertexAttribPointer(glGetAttribLocation(sFinal, "aPosition"), 2, GL_FLOAT, GL_FALSE, 0, vPositions);
    glEnableVertexAttribArray(glGetAttribLocation(sFinal, "aPosition"));
    GLfloat vTexCoords[] = {
        224.0/512.0, 0,
        224.0/512.0, 384.0/512.0,
        0, 0,
        0, 384.0/512.0};
    glVertexAttribPointer(glGetAttribLocation(sFinal, "aTexCoord"), 2, GL_FLOAT, GL_FALSE, 0, vTexCoords);
    glEnableVertexAttribArray(glGetAttribLocation(sFinal, "aTexCoord"));

    gpu_set_opaque(true);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gpu_set_opaque(false);
}
