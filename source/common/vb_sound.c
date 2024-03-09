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
    u8 shutoff_time, envelope_time, envelope_value, sample_pos;
    u16 freq_time;
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
static ndspWaveBuf wavebufs[2];

static const int noise_bits[8] = {14, 10, 13, 4, 8, 6, 9, 11};

#define RBYTE(x) (V810_SOUND_RAM.pmemory[(x) & 0xFFF])
#define GET_FREQ(ch) ((RBYTE(S1FQL + 0x40 * ch) | (RBYTE(S1FQH + 0x40 * ch) << 8)) & 0x7ff)
#define GET_FREQ_TIME(ch) ( \
    (2048 - (ch != 4 ? GET_FREQ(ch) : sound_state.sweep_frequency)) \
    * (ch == 5 ? 40 : 4))

static void fill_buf_single_sample(int ch, int samples, int offset) {
    ChannelState *channel = &sound_state.channels[ch];
    int lrv = RBYTE(S1LRV + 0x40 * ch);
    int left_vol = (channel->envelope_value * (lrv >> 4)) >> 3;
    int right_vol = (channel->envelope_value * (lrv & 0xf)) >> 3;
    s8 sample = 0;
    if (ch < 5) {
        sample = (RBYTE(0x80 * RBYTE(S1RAM + 0x40 * ch) + 4 * channel->sample_pos) << 2) ^ 0x80;
    } else {
        int bit = ~(sound_state.noise_shift >> 7);
        bit ^= sound_state.noise_shift >> noise_bits[(RBYTE(S6EV1) >> 4) & 7];
        sample = (bit & 1) ? 0x7c : 0x80;
    }
    u32 total = ((left_vol * sample) & 0xffff) | ((right_vol * sample) << 16);
    for (int i = 0; i < samples; i++) {
        ((u32*)(wavebufs[fill_buf].data_pcm16))[offset + i] += total;
    }
}

static void update_buf_with_freq(int ch, int samples) {
    if (!(RBYTE(S1INT + 0x40 * ch) & 0x80)) return;
    if (sound_state.channels[ch].envelope_value == 0) return;
    int total_clocks = samples * CYCLES_PER_SAMPLE;
    int current_clocks = 0;
    int freq_time = GET_FREQ_TIME(ch);
    while (current_clocks < total_clocks) {
        int clocks = total_clocks - current_clocks;
        // optimization for constant samples
        if (ch < 5 && constant_sample[RBYTE(S1RAM + 0x40 * ch)] < 0) {
            if (clocks > sound_state.channels[ch].freq_time)
                clocks = sound_state.channels[ch].freq_time;
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
                bit ^= sound_state.noise_shift >> noise_bits[(RBYTE(S6EV1) >> 4) & 7];
                sound_state.noise_shift = (sound_state.noise_shift << 1) | (bit & 1);
            }
            sound_state.channels[ch].freq_time = freq_time;
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
            update_buf_with_freq(i, samples);
        }
        if ((sound_state.effect_time -= samples) == 0) {
            sound_state.effect_time = 48;
            // sweep
            if (sound_state.modulation_enabled && (RBYTE(S5INT) & 0x80)) {
                int env = RBYTE(S5EV1);
                if ((env & 0x40) && --sound_state.sweep_time < 0) {
                    int swp = RBYTE(S5SWP);
                    int interval = (swp >> 4) & 7;
                    sound_state.sweep_time = interval * ((swp & 0x80) ? 8 : 1);
                    if (sound_state.sweep_time != 0) {
                        if (env & 0x10) {
                            // modulation
                            sound_state.sweep_frequency = GET_FREQ(4) + RBYTE(MODDATA + 4 * sound_state.modulation_counter++);
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
                                // TODO is this ok?
                                sound_state.channels[4].envelope_value = 0;
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
                int data = RBYTE(S1INT + 0x40 * i);
                if ((data & 0xa0) == 0xa0) {
                    if ((--sound_state.channels[i].shutoff_time & 0x1f) == 0x1f) {
                        // TODO is this ok?
                        sound_state.channels[i].envelope_value = 0;
                    }
                }
            }

            // envelope
            if (--sound_state.envelope_divider >= 0) goto effects_done;
            sound_state.envelope_divider += 4;
            for (int i = 0; i < 6; i++) {
                int data1 = RBYTE(S1EV1 + 0x40 * i);
                if (data1 & 1) {
                    if (--sound_state.channels[i].envelope_time & 8) {
                        int data0 = RBYTE(S1EV0 + 0x40 * i);
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
    }
}

// technically always u8 but gotta pass u16 because otherwise optimizations break everything
void sound_write(int addr, u16 data) {
    int ch = (addr >> 6) & 7;
    if ((addr & 0x3f) == (S1INT & 0x3f)) {
        int data = RBYTE(addr);
        sound_state.channels[ch].shutoff_time = data & 0x1f;
        sound_state.channels[ch].sample_pos = 0;
        sound_state.channels[ch].freq_time = GET_FREQ_TIME(ch);
        int ev0 = RBYTE(S1EV0 + 0x40 * ch);
        sound_state.channels[ch].envelope_value = ev0 >> 4;
        sound_state.channels[ch].envelope_time = ev0 & 7;
        if (ch == 4 && (RBYTE(S5EV1) & 0x40)) {
            // sweep/modulation
            int swp = RBYTE(S5SWP);
            int interval = (swp >> 4) & 7;
            sound_state.sweep_time = interval * ((swp & 0x80) ? 8 : 1);
            sound_state.sweep_frequency = GET_FREQ(4);
            sound_state.modulation_enabled = true;
            sound_state.modulation_counter = 0;
        } else if (ch == 6) {
            sound_state.noise_shift = 0;
        }
    } else if ((addr & 0x3f) == (S1EV0 & 0x3f)) {
        sound_state.channels[ch].envelope_value = (data >> 4) & 0xf;
    } else if (addr == S6EV1) {
        sound_state.noise_shift = 0;
    } else if (addr < 0x01000280) {
        int sample = (addr >> 7) & 7;
        constant_sample[sample] = RBYTE(0x80 * sample);
        for (int i = 1; i < 32; i++) {
            if (RBYTE(0x80 * sample + 4 * i) != constant_sample[sample]) {
                constant_sample[sample] = -1;
                break;
            }
        }
    }
    sound_update(v810_state->cycles);
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
