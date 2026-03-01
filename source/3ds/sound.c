#include <3ds.h>
#include <string.h>

#include "vb_gui.h"
#include "vb_set.h"
#include "vb_sound.h"
#include "vb_types.h"

static volatile bool paused = false;
static ndspWaveBuf hwbufs[BUF_COUNT];

void sound_callback(void *data) {
    if (paused) return;
    int last_buf = (sound_fill_buf + BUF_COUNT - 2) % BUF_COUNT;
    if (hwbufs[last_buf].status == NDSP_WBUF_DONE) {
        // uh oh, we're running out so repeat the last buf
        for (int buf = sound_fill_buf + 1; (buf %= BUF_COUNT) != sound_fill_buf; buf++) {
            if (hwbufs[last_buf].status != NDSP_WBUF_DONE) continue;
            memcpy(hwbufs[buf].data_pcm16, hwbufs[last_buf].data_pcm16, SAMPLE_COUNT * 4);
            DSP_FlushDataCache(hwbufs[buf].data_pcm16, sizeof(s16) * SAMPLE_COUNT * 2);
            ndspChnWaveBufAdd(0, &hwbufs[buf]);
        }
    }
}

bool sound_init_backend(s16 *wavebufs[]) {
    if (ndspInit()) {
        showSoundError();
        tVBOpt.SOUND = 0;
        return false;
    }
    ndspChnReset(0);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, SAMPLE_RATE);
    ndspSetCallback(sound_callback, NULL);
    // About as loud as it can be without clipping when all channels are max volume
    float mix[12] = {[0] = 1.5, [1] = 1.5};
    ndspChnSetMix(0, mix);
    for (int i = 0; i < BUF_COUNT; i++) {
        memset(&hwbufs[i], 0, sizeof(hwbufs[i]));
        hwbufs[i].data_pcm16 = wavebufs[i];
        hwbufs[i].nsamples = SAMPLE_COUNT;
        DSP_FlushDataCache(wavebufs[i], sizeof(s16) * SAMPLE_COUNT * 2);
        ndspChnWaveBufAdd(0, &hwbufs[i]);
    }
    return true;
}

void sound_close_backend() {
    ndspExit();
}

void sound_pause_backend() {
    paused = true;
    ndspChnWaveBufClear(0);
}

void sound_resume_backend() {
    paused = false;
}

bool sound_push_backend(int16_t *buf) {
    if (hwbufs[sound_fill_buf].status == NDSP_WBUF_DONE) {
        DSP_FlushDataCache(hwbufs[sound_fill_buf].data_pcm16, sizeof(int16_t) * SAMPLE_COUNT * 2);
        ndspChnWaveBufAdd(0, &hwbufs[sound_fill_buf]);
        return true;
    }
    return false;
}
