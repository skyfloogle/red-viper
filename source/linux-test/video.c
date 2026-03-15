#include "GLES2/gl2.h"

#include "vb_dsp.h"
#include "video_hard.h"
#include "v810_mem.h"

extern GLuint sFinal;

int eye_count = 2;

void video_hard_init(void);

void video_init(void) {
    video_hard_init();
}

extern GLuint screenTexHard[2];
void video_hard_render(int drawn_fb);

void video_render(int displayed_fb, bool on_time) {
    update_texture_cache_hard();
    video_hard_render(displayed_fb);

    // we need to have these caches during rendering
    tDSPCACHE.CharCacheInvalid = false;
    memset(tDSPCACHE.BGCacheInvalid, 0, sizeof(tDSPCACHE.BGCacheInvalid));
    memset(tDSPCACHE.CharacterCache, 0, sizeof(tDSPCACHE.CharacterCache));

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(sFinal);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, screenTexHard[displayed_fb]);
    glViewport(0, 0, 384*2, 224*2);
    glScissor(0, 0, 384*2, 224*2);

    glUniform1i(glGetUniformLocation(sFinal, "sTex"), 0);
    
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
