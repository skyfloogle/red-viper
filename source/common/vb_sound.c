#include <3ds.h>

#include "vb_gui.h"
#include "v810_mem.h"
#include "vb_set.h"
#include "vb_sound.h"
#include "vb_types.h"
#include <math.h>

#define SAMPLE_RATE 50000
#define CYCLES_PER_SAMPLE (20000000 / SAMPLE_RATE)
#define SAMPLE_COUNT (SAMPLE_RATE / 50)

typedef struct {
    uint8_t shutoff_time, envelope_time, envelope_value, sample_pos;
    uint16_t freq_time;
} ChannelState;
struct {
    ChannelState channels[6];
    int8_t sweep_time;
    int16_t sweep_frequency;
    bool modulation_enabled;
    int8_t modulation_values[32];
    uint8_t modulation_counter;
    uint16_t effect_time;
    uint32_t last_cycles;
} sound_state;

uint8_t fill_buf = 0;
uint16_t buf_pos = 0;
ndspWaveBuf wavebufs[2];

#define RBYTE(x) (V810_SOUND_RAM.pmemory[(x) & 0xFFF])

void fill_buf_single_sample(int ch, int samples, int offset) {
    ChannelState *channel = &sound_state.channels[ch];
    int lrv = RBYTE(S1LRV + 0x40 * ch);
    int left_vol = channel->envelope_value * (lrv >> 4);
    int right_vol = channel->envelope_value * (lrv & 0xf);
    s8 sample = 0;
    if (ch < 5) {
        sample = (RBYTE(0x80 * RBYTE(S1RAM + 0x40 * ch) + 4 * channel->sample_pos) << 2) ^ 0x80;
    }
    u32 total = ((left_vol * sample) << 16) | (right_vol * sample);
    for (int i = 0; i < samples; i++) {
        ((u32*)(wavebufs[fill_buf].data_pcm16))[offset + i] += total;
    }
}

void update_buf_with_freq(int ch, int samples) {
    if (!(RBYTE(S1INT + 0x40 * ch) & 0x80)) return;
    int total_clocks = samples * CYCLES_PER_SAMPLE;
    int current_clocks = 0;
    while (current_clocks < total_clocks) {
        int clocks = total_clocks - current_clocks;
        if (clocks > sound_state.channels[ch].freq_time)
            clocks = sound_state.channels[ch].freq_time;
        int current_samples = current_clocks / CYCLES_PER_SAMPLE;
        int next_samples = (current_clocks + clocks) / CYCLES_PER_SAMPLE;
        fill_buf_single_sample(ch, next_samples - current_samples, buf_pos + current_samples);
        if ((sound_state.channels[ch].freq_time -= clocks) == 0) {
            sound_state.channels[ch].sample_pos += 1;
            sound_state.channels[ch].sample_pos &= 31;
            int freq = RBYTE(S1FQL + 0x40 * ch) | (RBYTE(S1FQH + 0x40 * ch) << 8);
            sound_state.channels[ch].freq_time = (2048 - freq) * (ch == 5 ? 40 : 4);
        }
        current_clocks += clocks;
    }
}

void sound_update(int cycles) {
    int remaining_samples = (cycles - sound_state.last_cycles) / CYCLES_PER_SAMPLE;
    if (remaining_samples + buf_pos > SAMPLE_COUNT)
        remaining_samples = SAMPLE_COUNT - buf_pos;
    sound_state.last_cycles += remaining_samples * CYCLES_PER_SAMPLE;
    while (remaining_samples > 0) {
        int samples = remaining_samples;
        if (samples > sound_state.effect_time)
            samples = sound_state.effect_time;
        memset(wavebufs[fill_buf].data_pcm16 + buf_pos * 2, 0, sizeof(s16) * samples * 2);
        for (int i = 0; i < 6; i++) {
            sound_state.channels[i].envelope_value = RBYTE(S1EV0 + 0x40 * i) >> 4;
            update_buf_with_freq(i, samples);
        }
        if ((sound_state.effect_time -= samples) == 0) {
            sound_state.effect_time = 48;
        }
        buf_pos += samples;
        remaining_samples -= samples;
    }
}


void sound_init() {
    if (ndspInit()) {
        showSoundError();
        tVBOpt.SOUND = 0;
        return;
    }
    memset(&sound_state, 0, sizeof(sound_state));
    ndspChnReset(0);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetInterp(0, NDSP_INTERP_NONE);
    ndspChnSetRate(0, SAMPLE_RATE);
    float mix[12] = {[0] = 1, [1] = 1};
    ndspChnSetMix(0, mix);
    for (int i = 0; i < 2; i++) {
        memset(&wavebufs[i], 0, sizeof(wavebufs[i]));
        wavebufs[i].data_pcm16 = linearAlloc(sizeof(s16) * SAMPLE_COUNT * 2);
        wavebufs[i].nsamples = SAMPLE_COUNT;
        // force it to play the first time
        wavebufs[i].status = NDSP_WBUF_DONE;
    }
}

void sound_flush() {
    if (wavebufs[fill_buf].status == NDSP_WBUF_DONE) {
        DSP_FlushDataCache(wavebufs[fill_buf].data_pcm16, sizeof(s16) * SAMPLE_COUNT * 2);
        ndspChnWaveBufAdd(0, &wavebufs[fill_buf]);
        fill_buf = !fill_buf;
        buf_pos = 0;
    }
}

void sound_close() {
    for (int i = 0; i < 2; i++) linearFree(wavebufs[i].data_pcm16);
    ndspExit();
}
