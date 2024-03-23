#include <3ds.h>

#include "vb_gui.h"
#include "v810_mem.h"
#include "vb_set.h"
#include "vb_sound.h"
#include "vb_types.h"
#include <math.h>

#define SAMPLE_RATE 50000
#define CYCLES_PER_SAMPLE (20000000 / SAMPLE_RATE)
#define SAMPLE_COUNT (SAMPLE_RATE / 100)
#define BUF_COUNT 9

typedef struct {
    u8 shutoff_time, envelope_time, envelope_value, sample_pos;
    u32 freq_time;
} ChannelState;
static struct {
    ChannelState channels[6];
    bool modulation_enabled;
    u8 modulation_counter;
    s8 sweep_time;
    s16 sweep_frequency;
    u16 effect_time;
    u32 last_cycles;
    u16 noise_shift;
    s8 shutoff_divider, envelope_divider;
} sound_state;

static int constant_sample[5] = {-1, -1, -1, -1, -1};

static uint8_t fill_buf = 0;
static uint16_t buf_pos = 0;
static ndspWaveBuf wavebufs[BUF_COUNT];

static volatile bool paused = false;

static const int noise_bits[8] = {14, 10, 13, 4, 8, 6, 9, 11};

#define SNDMEM(x) (V810_SOUND_RAM.pmemory[(x) & 0xFFF])
#define GET_FREQ(ch) ((SNDMEM(S1FQL + 0x40 * ch) | (SNDMEM(S1FQH + 0x40 * ch) << 8)) & 0x7ff)
#define GET_FREQ_TIME(ch) ( \
    (2048 - (ch != 4 ? GET_FREQ(ch) : sound_state.sweep_frequency)) \
    * (ch == 5 ? 40 : 4))

static void fill_buf_single_sample(int ch, int samples, int offset) {
    ChannelState *channel = &sound_state.channels[ch];
    int lrv = SNDMEM(S1LRV + 0x40 * ch);
    int left_vol = (channel->envelope_value * (lrv >> 4)) >> 3;
    int right_vol = (channel->envelope_value * (lrv & 0xf)) >> 3;
    if (channel->envelope_value != 0) {
        // if neither stereo nor envelope is 0, increment amplitude
        if (lrv & 0xf0) left_vol++;
        if (lrv & 0x0f) right_vol++;
    }
    u8 sample;
    if (ch < 5) {
        sample = (SNDMEM(0x80 * (SNDMEM(S1RAM + 0x40 * ch) & 7) + 4 * channel->sample_pos) << 2);
    } else {
        int bit = ~(sound_state.noise_shift >> 7);
        bit ^= sound_state.noise_shift >> noise_bits[(SNDMEM(S6EV1) >> 4) & 7];
        sample = (bit & 1) ? 0x7c : 0x00;
    }
    u32 total = ((left_vol * sample) & 0xffff) | ((right_vol * sample) << 16);
    for (int i = 0; i < samples; i++) {
        ((u32*)(wavebufs[fill_buf].data_pcm16))[offset + i] += total;
    }
}

static void update_buf_with_freq(int ch, int samples) {
    if (!(SNDMEM(S1INT + 0x40 * ch) & 0x80)) return;
    if (sound_state.channels[ch].envelope_value == 0) return;
    if (ch < 5 && (SNDMEM(S1RAM + 0x40 * ch) & 7) >= 5) return;
    if (!tVBOpt.SOUND) return;
    int total_clocks = samples * CYCLES_PER_SAMPLE;
    int current_clocks = 0;
    int freq_time = GET_FREQ_TIME(ch);
    while (current_clocks < total_clocks) {
        int clocks = total_clocks - current_clocks;
        // optimization for constant samples
        if (ch == 5 || constant_sample[SNDMEM(S1RAM + 0x40 * ch) & 7] < 0) {
            if (clocks > sound_state.channels[ch].freq_time)
                clocks = sound_state.channels[ch].freq_time;
        } else {
            // constant sample, just reset the freqtime
            sound_state.channels[ch].freq_time = freq_time;
        }
        int current_samples = current_clocks / CYCLES_PER_SAMPLE;
        int next_samples = (current_clocks + clocks) / CYCLES_PER_SAMPLE;
        fill_buf_single_sample(ch, next_samples - current_samples, buf_pos + current_samples);
        if ((sound_state.channels[ch].freq_time -= clocks) == 0) {
            if (ch < 5) {
                sound_state.channels[ch].sample_pos += 1;
                sound_state.channels[ch].sample_pos &= 31;
            } else {
                int bit = ~(sound_state.noise_shift >> 7);
                bit ^= sound_state.noise_shift >> noise_bits[(SNDMEM(S6EV1) >> 4) & 7];
                sound_state.noise_shift = (sound_state.noise_shift << 1) | (bit & 1);
            }
            sound_state.channels[ch].freq_time = freq_time;
        }
        current_clocks += clocks;
    }
}

void sound_update(u32 cycles) {
    int remaining_samples = (cycles - sound_state.last_cycles) / CYCLES_PER_SAMPLE;
    if (remaining_samples <= 0) return;
    sound_state.last_cycles += remaining_samples * CYCLES_PER_SAMPLE;
    while (remaining_samples > 0) {
        int samples = remaining_samples;
        if (samples > SAMPLE_COUNT - buf_pos)
            samples = SAMPLE_COUNT - buf_pos;
        if (samples > sound_state.effect_time)
            samples = sound_state.effect_time;
        memset(wavebufs[fill_buf].data_pcm16 + buf_pos * 2, 0, sizeof(s16) * samples * 2);
        for (int i = 0; i < 6; i++) {
            update_buf_with_freq(i, samples);
        }
        if ((sound_state.effect_time -= samples) == 0) {
            sound_state.effect_time = 48;
            // sweep
            if (sound_state.modulation_enabled && (SNDMEM(S5INT) & 0x80)) {
                int env = SNDMEM(S5EV1);
                if ((env & 0x40) && --sound_state.sweep_time < 0) {
                    int swp = SNDMEM(S5SWP);
                    int interval = (swp >> 4) & 7;
                    sound_state.sweep_time = interval * ((swp & 0x80) ? 8 : 1);
                    if (sound_state.sweep_time != 0) {
                        if (env & 0x10) {
                            // modulation
                            sound_state.sweep_frequency = GET_FREQ(4) + SNDMEM(MODDATA + 4 * sound_state.modulation_counter++);
                            if (sound_state.modulation_counter >= 32) {
                                if (env & 0x20) {
                                    // repeat
                                    sound_state.modulation_counter = 0;
                                } else {
                                    sound_state.modulation_enabled = false;
                                }
                            }
                            sound_state.sweep_frequency &= 0x7ff;
                        } else {
                            // sweep
                            int shift = swp & 0x7;
                            if (swp & 8)
                                sound_state.sweep_frequency += sound_state.sweep_frequency >> shift;
                            else
                                sound_state.sweep_frequency -= sound_state.sweep_frequency >> shift;
                            if (sound_state.sweep_frequency <= 0 || sound_state.sweep_frequency >= 2048) {
                                SNDMEM(S5INT) &= ~0x80;
                                sound_state.modulation_enabled = false;
                            }
                        }
                    }
                }
            }
            
            // shutoff
            if (--sound_state.shutoff_divider >= 0) goto effects_done;
            sound_state.shutoff_divider += 4;
            for (int i = 0; i < 6; i++) {
                int data = SNDMEM(S1INT + 0x40 * i);
                if ((data & 0xa0) == 0xa0) {
                    if ((--sound_state.channels[i].shutoff_time & 0x1f) == 0x1f) {
                        SNDMEM(S1INT + 0x40 * i) &= ~0x80;
                    }
                }
            }

            // envelope
            if (--sound_state.envelope_divider >= 0) goto effects_done;
            sound_state.envelope_divider += 4;
            for (int i = 0; i < 6; i++) {
                int data1 = SNDMEM(S1EV1 + 0x40 * i);
                if (data1 & 1) {
                    if (--sound_state.channels[i].envelope_time & 8) {
                        int data0 = SNDMEM(S1EV0 + 0x40 * i);
                        sound_state.channels[i].envelope_time = data0 & 7;
                        sound_state.channels[i].envelope_value += (data0 & 8) ? 1 : -1;
                        if (sound_state.channels[i].envelope_value & 0x10) {
                            if (data1 & 2) {
                                sound_state.channels[i].envelope_value = data0 >> 4;
                            } else {
                                sound_state.channels[i].envelope_value -= (data0 & 8) ? 1 : -1;
                            }
                        }
                    }
                }
            }
        }
        effects_done:
        buf_pos += samples;
        remaining_samples -= samples;
        if (buf_pos == SAMPLE_COUNT) {
            // push
            if (wavebufs[fill_buf].status == NDSP_WBUF_DONE) {
                DSP_FlushDataCache(wavebufs[fill_buf].data_pcm16, sizeof(s16) * SAMPLE_COUNT * 2);
                ndspChnWaveBufAdd(0, &wavebufs[fill_buf]);
                fill_buf = (fill_buf + 1) % BUF_COUNT;
            }
            buf_pos = 0;
        }
    }
}

// technically always u8 but gotta pass u16 because otherwise optimizations break everything
void sound_write(int addr, u16 data) {
    sound_update(v810_state->cycles);
    SNDMEM(addr) = data;
    int ch = (addr >> 6) & 7;
    if (addr < 0x01000280) {
        int sample = (addr >> 7) & 7;
        constant_sample[sample] = SNDMEM(0x80 * sample);
        for (int i = 1; i < 32; i++) {
            if (SNDMEM(0x80 * sample + 4 * i) != constant_sample[sample]) {
                constant_sample[sample] = -1;
                break;
            }
        }
    } else if (addr < 0x01000400) {
        // ignore
    } else if (addr == SSTOP && (data & 1)) {
        for (int i = 0; i < 6; i++) {
            SNDMEM(S1INT + 0x40 * i) &= ~0x80;
        }
    } else if ((addr & 0x3f) == (S1INT & 0x3f)) {
        if (ch == 4) {
            sound_state.sweep_frequency = GET_FREQ(4);
            if (SNDMEM(S5EV1) & 0x40) {
                // sweep/modulation
                int swp = SNDMEM(S5SWP);
                int interval = (swp >> 4) & 7;
                sound_state.sweep_time = interval * ((swp & 0x80) ? 8 : 1);
                sound_state.modulation_enabled = true;
                sound_state.modulation_counter = 0;
            }
        } else if (ch == 5) {
            sound_state.noise_shift = 0;
        }
        sound_state.channels[ch].shutoff_time = data & 0x1f;
        sound_state.channels[ch].sample_pos = 0;
        sound_state.channels[ch].freq_time = GET_FREQ_TIME(ch);
        int ev0 = SNDMEM(S1EV0 + 0x40 * ch);
        sound_state.channels[ch].envelope_value = ev0 >> 4;
        sound_state.channels[ch].envelope_time = ev0 & 7;
    } else if ((addr & 0x3f) == (S1EV0 & 0x3f)) {
        sound_state.channels[ch].envelope_value = (data >> 4) & 0xf;
    } else if (addr == S5FQL || addr == S5FQH) {
        if (ch == 4) sound_state.sweep_frequency = GET_FREQ(4);
    } else if (addr == S6EV1) {
        sound_state.noise_shift = 0;
    }
}

void sound_callback(void *data) {
    if (paused) return;
    int last_buf = (fill_buf + BUF_COUNT - 2) % BUF_COUNT;
    if (wavebufs[last_buf].status == NDSP_WBUF_DONE) {
        // uh oh, we're running out so repeat the last buf
        for (int buf = fill_buf + 1; (buf %= BUF_COUNT) != fill_buf; buf++) {
            if (wavebufs[last_buf].status != NDSP_WBUF_DONE) continue;
            memcpy(wavebufs[buf].data_pcm16, wavebufs[last_buf].data_pcm16, SAMPLE_COUNT * 4);
            ndspChnWaveBufAdd(0, &wavebufs[buf]);
        }
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
    ndspSetCallback(sound_callback, NULL);
    // About as loud as it can be without clipping when all channels are max volume
    float mix[12] = {[0] = 1.37, [1] = 1.37};
    ndspChnSetMix(0, mix);
    for (int i = 0; i < BUF_COUNT; i++) {
        memset(&wavebufs[i], 0, sizeof(wavebufs[i]));
        wavebufs[i].data_pcm16 = linearAlloc(sizeof(s16) * SAMPLE_COUNT * 2);
        wavebufs[i].nsamples = SAMPLE_COUNT;
        ndspChnWaveBufAdd(0, &wavebufs[i]);
    }
}

void sound_close() {
    ndspExit();
    for (int i = 0; i < BUF_COUNT; i++) linearFree(wavebufs[i].data_pcm16);
}

void sound_pause() {
    paused = true;
    ndspChnWaveBufClear(0);
}

void sound_resume() {
    paused = false;
}

void sound_reset() {
    memset(&sound_state, 0, sizeof(sound_state));
    for (int i = 0; i < 6; i++) {
        SNDMEM(S1INT + 0x40 * i) = 0;
    }
    for (int i = 0; i < BUF_COUNT; i++) {
        memset(wavebufs[i].data_pcm16, 0, SAMPLE_COUNT * 4);
    }
    paused = false;
}
