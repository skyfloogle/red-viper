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

SOUND_STATE sound_state;

static int constant_sample[5] = {-1, -1, -1, -1, -1};

static uint8_t fill_buf = 0;
static uint16_t buf_pos = 0;
static ndspWaveBuf wavebufs[BUF_COUNT];

static volatile bool paused = false;

static const int noise_bits[8] = {14, 10, 13, 4, 8, 6, 9, 11};

static short dc_offset = 0;

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
        sample = SNDMEM(0x80 * (SNDMEM(S1RAM + 0x40 * ch) & 7) + 4 * channel->sample_pos) & 63;
    } else {
        int bit = ~(sound_state.noise_shift >> 7);
        bit ^= sound_state.noise_shift >> noise_bits[(SNDMEM(S6EV1) >> 4) & 7];
        sample = (bit & 1) ? 0x3f : 0x00;
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
            sound_state.channels[ch].freq_time = clocks + freq_time;
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

void sound_update(uint32_t cycles) {
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

        // early sweep frequency and shutoff
        int new_sweep_frequency = sound_state.sweep_frequency;
        if ((SNDMEM(S5INT) & 0x80) && !(SNDMEM(S5EV1) & 0x10)) {
            int swp = SNDMEM(S5SWP);
            int shift = swp & 0x7;
            if (swp & 8) {
                new_sweep_frequency += sound_state.sweep_frequency >> shift;
                if (new_sweep_frequency >= 2048) SNDMEM(S5INT) = 0;
            } else {
                new_sweep_frequency -= sound_state.sweep_frequency >> shift;
                if (new_sweep_frequency < 0) new_sweep_frequency = 0;
            }
        }

        for (int i = 0; i < 6; i++) {
            update_buf_with_freq(i, samples);
        }
        if ((sound_state.effect_time -= samples) == 0) {
            sound_state.effect_time = 48;
            // sweep
            if (SNDMEM(S5INT) & 0x80) {
                int env = SNDMEM(S5EV1);
                if ((env & 0x40) && --sound_state.sweep_time < 0) {
                    int swp = SNDMEM(S5SWP);
                    int interval = (swp >> 4) & 7;
                    sound_state.sweep_time = interval * ((swp & 0x80) ? 8 : 1);
                    if (sound_state.sweep_time != 0) {
                        if (env & 0x10) {
                            // modulation
                            // only enable on first loop or if repeat
                            if (sound_state.modulation_state == 0 || (env & 0x20)) {
                                sound_state.sweep_frequency = GET_FREQ(4) + (s8)SNDMEM(MODDATA + 4 * sound_state.modulation_counter);
                                if (sound_state.sweep_frequency < 0) sound_state.sweep_frequency = 0;
                                if (sound_state.sweep_frequency > 0x7ff) sound_state.sweep_frequency = 0x7ff;
                            }
                            if (sound_state.modulation_state == 1) sound_state.modulation_state = 2;
                        } else if (sound_state.modulation_state < 2) {
                            // sweep using old calculation
                            sound_state.sweep_frequency = new_sweep_frequency;
                        }
                        if (++sound_state.modulation_counter >= 32) {
                            if (sound_state.modulation_state == 0) sound_state.modulation_state = 1;
                            sound_state.modulation_counter = 0;
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
                if (!(SNDMEM(S1INT + 0x40 * i) & 0x80)) continue;
                int data1 = SNDMEM(S1EV1 + 0x40 * i);
                if ((data1 & 1) && !(sound_state.channels[i].envelope_time & 128)) {
                    if (--sound_state.channels[i].envelope_time & 8) {
                        int data0 = SNDMEM(S1EV0 + 0x40 * i);
                        sound_state.channels[i].envelope_time = data0 & 7;
                        sound_state.channels[i].envelope_value += (data0 & 8) ? 1 : -1;
                        if (sound_state.channels[i].envelope_value & 0x10) {
                            if (data1 & 2) {
                                sound_state.channels[i].envelope_value = data0 >> 4;
                            } else {
                                sound_state.channels[i].envelope_value -= (data0 & 8) ? 1 : -1;
                                sound_state.channels[i].envelope_time = 128;
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
            // final post processing
            for (int i = 0; i < SAMPLE_COUNT; i++) {
                #define AMPLIFY(x) (((x) >> 4) * 95)
                short left = AMPLIFY(wavebufs[fill_buf].data_pcm16[i * 2]) + dc_offset;
                short right = AMPLIFY(wavebufs[fill_buf].data_pcm16[i * 2 + 1]) + dc_offset;
                #undef AMPLIFY
                int extra_offset = dc_offset - (-left - right + dc_offset * 48) / 50;
                if (left < dc_offset || right < dc_offset) {
                    int extra_offset = 0;
                    if (left < dc_offset)
                        extra_offset = left - 0x7fff;
                    if (right < dc_offset && right - 0x7fff > extra_offset)
                        extra_offset = right - 0x7fff;
                }
                left -= extra_offset;
                right -= extra_offset;
                dc_offset -= extra_offset;
                if (dc_offset != 0) {
                    wavebufs[fill_buf].data_pcm16[i * 2] = left;
                    wavebufs[fill_buf].data_pcm16[i * 2 + 1] = right;
                }
            }
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
void sound_write(int addr, uint16_t data) {
    if (addr & 3) return;
    sound_update(v810_state->cycles);
    if (!(addr & 0x400)) {
        // ram writes, these can be declined
        // all ram writes are declined if channel 5 is active
        if (SNDMEM(S5INT) & 0x80) return;
        if ((addr & 0x370) < 0x280) {
            // wave ram is declined if any channel is active
            if ((SNDMEM(S1INT) & 0x80) ||
                (SNDMEM(S2INT) & 0x80) ||
                (SNDMEM(S3INT) & 0x80) ||
                (SNDMEM(S4INT) & 0x80) ||
                (SNDMEM(S6INT) & 0x80)) return;
        }
    }
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
    } else if (addr == SSTOP) {
        if (data & 1) {
            for (int i = 0; i < 6; i++) {
                SNDMEM(S1INT + 0x40 * i) &= ~0x80;
            }
        }
    } else if ((addr & 0x3f) == (S1INT & 0x3f)) {
        if (ch == 4) {
            if (SNDMEM(S5EV1) & 0x40) {
                // sweep/modulation
                int swp = SNDMEM(S5SWP);
                int interval = (swp >> 4) & 7;
                sound_state.sweep_time = interval * ((swp & 0x80) ? 8 : 1);
                sound_state.modulation_counter = 0;
                sound_state.modulation_state = 0;
            }
        } else if (ch == 5) {
            sound_state.noise_shift = 0;
        }
        sound_state.channels[ch].shutoff_time = data & 0x1f;
        sound_state.channels[ch].sample_pos = 0;
        sound_state.channels[ch].freq_time = GET_FREQ_TIME(ch);
        int ev0 = SNDMEM(S1EV0 + 0x40 * ch);
        sound_state.channels[ch].envelope_time = ev0 & 7;
    } else if ((addr & 0x3f) == (S1EV0 & 0x3f)) {
        sound_state.channels[ch].envelope_value = (data >> 4) & 0xf;
    } else if (addr == S5FQL) {
        ((uint8_t*)&sound_state.sweep_frequency)[0] = data;
    } else if (addr == S5FQH) {
        ((uint8_t*)&sound_state.sweep_frequency)[1] = data & 0x7;
    } else if (addr == S6EV1) {
        sound_state.noise_shift = 0;
    }
}

void sound_refresh(void) {
    for (int sample = 0; sample < 5; sample++) {
        constant_sample[sample] = SNDMEM(0x80 * sample);
        for (int i = 1; i < 32; i++) {
            if (SNDMEM(0x80 * sample + 4 * i) != constant_sample[sample]) {
                constant_sample[sample] = -1;
                break;
            }
        }
    }
    for (int i = 0; i < BUF_COUNT; i++) {
        memset(wavebufs[i].data_pcm16, 0, SAMPLE_COUNT * 4);
    }
    paused = false;
}

void sound_callback(void *data) {
    if (paused) return;
    int last_buf = (fill_buf + BUF_COUNT - 2) % BUF_COUNT;
    if (wavebufs[last_buf].status == NDSP_WBUF_DONE) {
        // uh oh, we're running out so repeat the last buf
        for (int buf = fill_buf + 1; (buf %= BUF_COUNT) != fill_buf; buf++) {
            if (wavebufs[last_buf].status != NDSP_WBUF_DONE) continue;
            memcpy(wavebufs[buf].data_pcm16, wavebufs[last_buf].data_pcm16, SAMPLE_COUNT * 4);
            DSP_FlushDataCache(wavebufs[buf].data_pcm16, sizeof(s16) * SAMPLE_COUNT * 2);
            ndspChnWaveBufAdd(0, &wavebufs[buf]);
        }
    }
}

void sound_init(void) {
    if (ndspInit()) {
        showSoundError();
        tVBOpt.SOUND = 0;
        return;
    }
    memset(&sound_state, 0, sizeof(sound_state));
    ndspChnReset(0);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, SAMPLE_RATE);
    ndspSetCallback(sound_callback, NULL);
    // About as loud as it can be without clipping when all channels are max volume
    float mix[12] = {[0] = 1.5, [1] = 1.5};
    ndspChnSetMix(0, mix);
    for (int i = 0; i < BUF_COUNT; i++) {
        memset(&wavebufs[i], 0, sizeof(wavebufs[i]));
        wavebufs[i].data_pcm16 = linearAlloc(sizeof(s16) * SAMPLE_COUNT * 2);
        wavebufs[i].nsamples = SAMPLE_COUNT;
        DSP_FlushDataCache(wavebufs[i].data_pcm16, sizeof(s16) * SAMPLE_COUNT * 2);
        ndspChnWaveBufAdd(0, &wavebufs[i]);
    }
}

void sound_close(void) {
    ndspExit();
    for (int i = 0; i < BUF_COUNT; i++) linearFree(wavebufs[i].data_pcm16);
}

void sound_pause(void) {
    paused = true;
    dc_offset = 0;
    ndspChnWaveBufClear(0);
}

void sound_resume(void) {
    paused = false;
}

void sound_reset(void) {
    memset(&sound_state, 0, sizeof(sound_state));
    for (int i = 0; i < 6; i++) {
        SNDMEM(S1INT + 0x40 * i) = 0;
    }
    sound_refresh();
}
