#include <stdbool.h>
#include <stdio.h>

#include <3ds.h>

#include "allegro_compat.h"
#include "vb_types.h"
#include "vb_set.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_sound.h"
#include "vb_lfsr.h"

#define CH1       0
#define CH2       1
#define CH3       2
#define CH4       3
#define CH5       4
#define CH6_0     5
#define CH6_1     6
#define CH6_2     7
#define CH6_3     8
#define CH6_4     9
#define CH6_5    10
#define CH6_6    11
#define CH6_7    12
#define CH_TOTAL 13

SAMPLE* channel[CH_TOTAL];
int voice[CH_TOTAL];
int Curr_C6V, C6V_playing = 0;
int snd_ram_changed[6] = {0, 0, 0, 0, 0, 0};

BYTE* Noise_Opt[8] = {Noise_Opt0, Noise_Opt1, Noise_Opt2, Noise_Opt3, Noise_Opt4, Noise_Opt5, Noise_Opt6, Noise_Opt7};
int Noise_Opt_Size[8] = {OPT0LEN, OPT1LEN, OPT2LEN, OPT3LEN, OPT4LEN, OPT5LEN, OPT6LEN, OPT7LEN};

// FRQ reg converted to sampling frq for allegro
// manual says up to 2040, but any higher than 2038 crashes RB
// frequency * 32 samples per cycle
#define VB_FRQ_REG_TO_SAMP_FREQ(v) (5000000/(2048-(((v)>2038)?2038:(v))))
//#define VB_FRQ_REG_TO_SAMP_FREQ(v) (5000000/(2048-((v)*32)))

// Noise FRQ reg converted to sampling frq for allegro
#define RAND_FRQ_REG_TO_SAMP_FREQ(v) (500000/(2048-(v)))

bool sound_running = false;
Thread soundThread;
uint8_t shutoff_intervals[6];
uint8_t envelope_intervals[6];
uint8_t envelope_values[6];
int8_t sweep_interval;
int16_t sweep_frequency;
bool modulation_enabled = false;
int8_t modulation_values[32];
uint8_t modulation_counter = 0;
void sound_thread() {
    int shutoff_divider = 0;
    int envelope_divider = 0;
    u64 lastTime = svcGetSystemTick();
    while (sound_running) {
        u64 newTime = svcGetSystemTick();
        s64 waitNanos = 960000 - 1000 * (newTime - lastTime) / CPU_TICKS_PER_USEC;
        if (waitNanos > 0)
            svcSleepThread(waitNanos);
        lastTime = newTime;
        // do sweep
        if (modulation_enabled && mem_rbyte(S5INT) & 0x80) {
            int env = mem_rbyte(S5EV1);
            if ((env & 0x40) && --sweep_interval < 0) {
                int swp = mem_rbyte(S5SWP);
                int interval = (swp >> 4) & 7;
                sweep_interval = interval * ((swp & 0x80) ? 8 : 1);
                if (sweep_interval != 0) {
                    if (env & 0x10) {
                        // modulation
                        sweep_frequency += modulation_values[modulation_counter++];
                        if (modulation_counter >= 32) {
                            if (env & 0x20) {
                                // repeat
                                modulation_counter = 0;
                            } else {
                                modulation_enabled = false;
                            }
                        }
                        sweep_frequency &= 0x7ff;
                        voice_set_frequency(voice[CH5], VB_FRQ_REG_TO_SAMP_FREQ(sweep_frequency));
                    } else {
                        // sweep
                        int shift = swp & 7;
                        if (swp & 8)
                            sweep_frequency += sweep_frequency >> shift;
                        else
                            sweep_frequency -= sweep_frequency >> shift;
                        if (sweep_frequency < 0 || sweep_frequency >= 2048) {
                            voice_stop(voice[CH5]);
                            modulation_enabled = false;
                        } else {
                            voice_set_frequency(voice[CH5], VB_FRQ_REG_TO_SAMP_FREQ(sweep_frequency));
                        }
                    }
                }
            }
        }
        if (--shutoff_divider >= 0) continue;
        shutoff_divider += 4;
        // do shutoff
        for (int i = 0; i < 6; i++) {
            int addr = S1INT + 0x40 * i;
            int data = mem_rbyte(addr);
            if ((data & 0xa0) == 0xa0) {
                if ((--shutoff_intervals[i] & 0x1f) == 0x1f) {
                    voice_stop(voice[i]);
                    data &= ~0x80;
                    mem_wbyte(addr, data);
                }
            }
        }
        if (--envelope_divider >= 0) continue;
        envelope_divider += 4;
        for (int i = 0; i < 6; i++) {
            int data1 = mem_rbyte(S1EV1 + 0x40 * i);
            if (data1 & 1) {
                if (--envelope_intervals[i] & 8) {
                    int data0 = mem_rbyte(S1EV0 + 0x40 * i);
                    envelope_intervals[i] = data0 & 7;
                    envelope_values[i] += (data0 & 8) ? 1 : -1;
                    if (envelope_values[i] & 0x10) {
                        if (data1 & 2) {
                            envelope_values[i] = data0 >> 4;
                        } else {
                            envelope_values[i] -= (data0 & 8) ? 1 : -1;
                            if (envelope_values[i] == 0) voice_stop(i);
                        }
                    }
                    int lr = mem_rbyte(S1LRV + 0x40 * i);
                    voice_set_volume(voice[i], (lr >> 4) * envelope_values[i], (lr & 0x0F) * envelope_values[i]);
                }
                
            }
        }
    }
}

// Set up Allegro sound stuff
void sound_init() {
    int i, index;

    if (!tVBOpt.SOUND)
        return;

    if (-1 == install_sound(DIGI_AUTODETECT, MIDI_NONE, NULL)) {
        dprintf(0, "[SND]: Error installing sound\n");
        tVBOpt.SOUND = 0;
        return;
    }

    for (i = CH1; i <= CH5; ++i) {
        channel[i] = create_sample(8, 0, 0, 32);
    }

    for (i = CH6_0; i <= CH6_7; ++i) {
        index = i - CH6_0;
        channel[i] = create_sample(8, 0, 0, Noise_Opt_Size[index]);
        memcpy(channel[i]->data, Noise_Opt[index], Noise_Opt_Size[index]);
    }

    for (i = 0; i < CH_TOTAL; ++i) {
        voice[i] = allocate_voice(channel[i]);
        voice_set_playmode(voice[i], PLAYMODE_LOOP);
    }

    // Set default to 0
    Curr_C6V = voice[CH6_0];

    sound_running = true;

    APT_SetAppCpuTimeLimit(30);
    if (!(soundThread = threadCreate(sound_thread, NULL, 4000, 0x18, 1, true)))
        printf("couldn't make sound thread\n");
}

// Close Allegro sound stuff
void sound_close() {
    int i;

    if (!tVBOpt.SOUND)
        return;

    sound_running = false;
    threadJoin(soundThread, U64_MAX);

    for (i = 0; i < CH_TOTAL; ++i) {
        voice_stop(voice[i]);
        destroy_sample(channel[i]);
        deallocate_voice(voice[i]);
    }
    remove_sound();
}

// Handles updating allegro sounds according VB sound regs
void sound_update(int reg) {
    BYTE reg1, reg2; // Temporary regs
    int i, temp1, temp2;
    char waveram[32];
    const unsigned int wavelut[5] = {0x01000000, 0x01000080, 0x01000100, 0x01000180, 0x01000200};

    if (!tVBOpt.SOUND)
        return;

    // Notify of change in sound ram
    // Should check to make sure all channels disabled first (required on VB hardware)
    switch (reg & 0xFFFFFF80) {
        case WAVEDATA1:
            snd_ram_changed[0] = 1;
            break;
        case WAVEDATA2:
            snd_ram_changed[1] = 1;
            break;
        case WAVEDATA3:
            snd_ram_changed[2] = 1;
            break;
        case WAVEDATA4:
            snd_ram_changed[3] = 1;
            break;
        case WAVEDATA5:
            snd_ram_changed[4] = 1;
            break;
        case MODDATA:
            snd_ram_changed[5] = 1;
            break;
        default:
            break;
    }

    switch (reg) {
        // Channel 1
        case S1INT:
            reg1 = mem_rbyte(reg);
            if (reg1 & 0x80) {
                // Can only change RAM when disabled, so re-copy on enable in case sound RAM contents are changed
                if (snd_ram_changed[mem_rbyte(S1RAM)]) {
                    for (i = 0; i < 32; i++)
                        // Make 8 bit samples out of 6 bit samples
                        waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(S1RAM)] + (i << 2)) << 2) ^ ((char)0x80);
                    memcpy(channel[CH1]->data, waveram, 32);
                    snd_ram_changed[mem_rbyte(S1RAM)] = 0;
                    // Output according to interval data
                }
                voice_set_position(voice[CH1], 0);
                reg2 = mem_rbyte(S1EV0);
                envelope_intervals[0] = reg2 & 7;
                voice_start(voice[CH1]);
            } else {
                voice_stop(voice[CH1]);
            }
            break;
        case S1LRV:
        case S1EV0:
            reg1 = mem_rbyte(S1LRV);
            if (reg == S1EV0) {
                reg2 = mem_rbyte(S1EV0);
                envelope_values[0] = reg2 >> 4;
            }
            voice_set_volume(voice[CH1], (reg1 >> 4) * envelope_values[0], (reg1 & 0x0F) * envelope_values[0]);
            break;
        case S1FQL:
        case S1FQH:
            reg1 = mem_rbyte(S1FQL);
            reg2 = mem_rbyte(S1FQH);
            voice_set_frequency(voice[CH1], VB_FRQ_REG_TO_SAMP_FREQ(((reg2 << 8) | reg1) & 0x7FF));
            break;
        case S1RAM:
            for (i = 0; i < 32; i++)
                // Make 8 bit samples out of 6 bit samples
                waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(reg)] + (i << 2)) << 2) ^ ((char)0x80);
            memcpy(channel[CH1]->data, waveram, 32);
            break;

            // Channel 2
        case S2INT:
            reg1 = mem_rbyte(reg);
            if (reg1 & 0x80) {
                // Can only change RAM when disabled, so re-copy on enable in case sound RAM contents are changed
                if (snd_ram_changed[mem_rbyte(S2RAM)]) {
                    for (i = 0; i < 32; i++)
                        // Make 8 bit samples out of 6 bit samples
                        waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(S2RAM)] + (i << 2)) << 2) ^ ((char)0x80);
                    memcpy(channel[CH2]->data, waveram, 32);
                    snd_ram_changed[mem_rbyte(S2RAM)] = 0;
                }
                voice_set_position(voice[CH2], 0);
                reg2 = mem_rbyte(S2EV0);
                envelope_intervals[1] = reg2 & 7;
                voice_start(voice[CH2]);
            } else {
                voice_stop(voice[CH2]);
            }
            break;
        case S2LRV:
        case S2EV0:
            reg1 = mem_rbyte(S2LRV);
            if (reg == S2EV0) {
                reg2 = mem_rbyte(S2EV0);
                envelope_values[1] = reg2 >> 4;
            }
            voice_set_volume(voice[CH2], (reg1 >> 4) * envelope_values[1], (reg1 & 0x0F) * envelope_values[1]);
            break;
        case S2FQL:
        case S2FQH:
            reg1 = mem_rbyte(S2FQL);
            reg2 = mem_rbyte(S2FQH);
            voice_set_frequency(voice[CH2], VB_FRQ_REG_TO_SAMP_FREQ(((reg2 << 8) | reg1) & 0x7FF));
            break;
        case S2RAM:
            for (i = 0; i < 32; i++)
                // Make 8 bit samples out of 6 bit samples
                waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(reg)] + (i << 2)) << 2) ^ ((char)0x80);
            memcpy(channel[CH2]->data, waveram, 32);
            break;

            // Channel 3
        case S3INT:
            reg1 = mem_rbyte(reg);
            if (reg1 & 0x80) {
                // Can only change RAM when disabled, so re-copy on enable in case sound RAM contents are changed
                if (snd_ram_changed[mem_rbyte(S3RAM)]) {
                    for (i = 0; i < 32; i++)
                        // Make 8 bit samples out of 6 bit samples
                        waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(S3RAM)] + (i << 2)) << 2) ^ ((char)0x80);
                    memcpy(channel[CH3]->data, waveram, 32);
                    snd_ram_changed[mem_rbyte(S3RAM)] = 0;
                }
                voice_set_position(voice[CH3], 0);
                reg2 = mem_rbyte(S3EV0);
                envelope_intervals[2] = reg2 & 7;
                voice_start(voice[CH3]);
            } else {
                voice_stop(voice[CH3]);
            }
            break;
        case S3LRV:
        case S3EV0:
            reg1 = mem_rbyte(S3LRV);
            if (reg == S3EV0) {
                reg2 = mem_rbyte(S3EV0);
                envelope_values[2] = reg2 >> 4;
            }
            voice_set_volume(voice[CH3], (reg1 >> 4) * envelope_values[2], (reg1 & 0x0F) * envelope_values[2]);
            break;
        case S3FQL:
        case S3FQH:
            reg1 = mem_rbyte(S3FQL);
            reg2 = mem_rbyte(S3FQH);
            voice_set_frequency(voice[CH3], VB_FRQ_REG_TO_SAMP_FREQ(((reg2 << 8) | reg1) & 0x7FF));
            break;
        case S3RAM:
            for (i = 0; i < 32; i++)
                // Make 8 bit samples out of 6 bit samples
                waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(reg)] + (i << 2)) << 2) ^ ((char)0x80);
            memcpy(channel[CH3]->data, waveram, 32);
            break;

            // Channel 4
        case S4INT:
            reg1 = mem_rbyte(reg);
            if (reg1 & 0x80) {
                // Can only change RAM when disabled, so re-copy on enable in case sound RAM contents are changed
                if (snd_ram_changed[mem_rbyte(S4RAM)]) {
                    for (i = 0; i < 32; i++)
                        // Make 8 bit samples out of 6 bit samples
                        waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(S4RAM)] + (i << 2)) << 2) ^ ((char)0x80);
                    memcpy(channel[CH4]->data, waveram, 32);
                    snd_ram_changed[mem_rbyte(S4RAM)] = 0;
                }
                voice_set_position(voice[CH4], 0);
                reg2 = mem_rbyte(S4EV0);
                envelope_intervals[3] = reg2 & 7;
                voice_start(voice[CH4]);
            } else {
                voice_stop(voice[CH4]);
            }
            break;
        case S4LRV:
        case S4EV0:
            reg1 = mem_rbyte(S4LRV);
            if (reg == S4EV0) {
                reg2 = mem_rbyte(S4EV0);
                envelope_values[3] = reg2 >> 4;
            }
            voice_set_volume(voice[CH4], (reg1 >> 4) * envelope_values[3], (reg1 & 0x0F) * envelope_values[3]);
            break;
        case S4FQL:
        case S4FQH:
            reg1 = mem_rbyte(S4FQL);
            reg2 = mem_rbyte(S4FQH);
            voice_set_frequency(voice[CH4], VB_FRQ_REG_TO_SAMP_FREQ(((reg2 << 8) | reg1) & 0x7FF));
            break;
        case S4RAM:
            for (i = 0; i < 32; i++)
                // Make 8 bit samples out of 6 bit samples
                waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(reg)] + (i << 2)) << 2) ^ ((char)0x80);
            memcpy(channel[CH4]->data, waveram, 32);
            break;

            // Channel 5
        case S5INT:
            reg1 = mem_rbyte(reg);
            if (reg1 & 0x80) {
                // Can only change RAM when disabled, so re-copy on enable in case sound RAM contents are changed
                if (snd_ram_changed[mem_rbyte(S5RAM)]) {
                    for (i = 0; i < 32; i++)
                        // Make 8 bit samples out of 6 bit samples
                        waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(S5RAM)] + (i << 2)) << 2) ^ ((char)0x80);
                    memcpy(channel[CH5]->data, waveram, 32);
                    snd_ram_changed[mem_rbyte(S5RAM)] = 0;
                }
                voice_set_position(voice[CH5], 0);
                reg2 = mem_rbyte(S5EV0);
                envelope_intervals[4] = reg2 & 7;
                reg2 = mem_rbyte(S5EV1);
                if (reg2 & 0x40) {
                    // sweep/modulation
                    reg2 = mem_rbyte(S5SWP);
                    int interval = (reg2 >> 4) & 7;
                    sweep_interval = interval * ((reg2 & 0x80) ? 8 : 1);
                    sweep_frequency = ((mem_rbyte(S5FQH) << 8) | mem_rbyte(S5FQL)) & 0x7ff;
                    modulation_enabled = true;
                    if (reg2 & 0x10) {
                        for (int i = 0; i < 32; i++) {
                            modulation_values[i] = mem_rbyte(MODDATA + i * 4);
                        }
                    }
                }
                voice_start(voice[CH5]);
            } else {
                voice_stop(voice[CH5]);
            }
            break;
        case S5LRV:
        case S5EV0:
        case S5EV1:
        case S5SWP:
            reg1 = mem_rbyte(S5LRV);
            if (reg == S5EV0) {
                reg2 = mem_rbyte(S5EV0);
                envelope_values[4] = reg2 >> 4;
            }
            voice_set_volume(voice[CH5], (reg1 >> 4) * envelope_values[4], (reg1 & 0x0F) * envelope_values[4]);
            break;
        case S5FQL:
        case S5FQH:
            reg1 = mem_rbyte(S5FQL);
            reg2 = mem_rbyte(S5FQH);
            voice_set_frequency(voice[CH5], VB_FRQ_REG_TO_SAMP_FREQ(((reg2 << 8) | reg1) & 0x7FF));
            break;
        case S5RAM:
            for (i = 0; i < 32; i++)
                // Make 8 bit samples out of 6 bit samples
                waveram[i] = (char) (mem_rbyte(wavelut[mem_rbyte(reg)] + (i << 2)) << 2) ^ ((char)0x80);
            memcpy(channel[CH5]->data, waveram, 32);
            break;

            // Channel 6
        case S6INT:
            reg1 = mem_rbyte(reg);
            if (reg1 & 0x80) {
                voice_set_position(Curr_C6V, 0);
                reg2 = mem_rbyte(S6EV0);
                envelope_intervals[5] = reg2 & 7;
                voice_start(Curr_C6V);
                C6V_playing = 1;
            } else {
                voice_stop(Curr_C6V);
                C6V_playing = 0;
            }
            break;
        case S6LRV:
        case S6EV0:
            reg1 = mem_rbyte(S6LRV);
            if (reg == S6EV0) {
                reg2 = mem_rbyte(S6EV0);
                envelope_values[5] = reg2 >> 4;
            }
            voice_set_volume(voice[Curr_C6V], (reg1 >> 4) * envelope_values[5], (reg1 & 0x0F) * envelope_values[5]);

            // Changing LFSR voice?  Rather than recopy LFSR sequence and
            // handle sequences of different lengths, just switch between
            // pre-allocated LFSR voices
            reg1 = mem_rbyte(S6EV1);
            temp1 = ((reg1 >> 4) & 0x07);
            temp1 += CH6_0;
            // temp1 is one of ch6_0 to ch_6_7
            if (Curr_C6V != voice[temp1]) {
                voice_stop(Curr_C6V);
                Curr_C6V = voice[temp1];
                if (C6V_playing == 1)
                    voice_start(Curr_C6V);
            }
            break;
        case S6FQL:
        case S6FQH:
            reg1 = mem_rbyte(S6FQL);
            reg2 = mem_rbyte(S6FQH);
            voice_set_frequency(Curr_C6V, RAND_FRQ_REG_TO_SAMP_FREQ(((reg2 << 8) | reg1) & 0x7FF));
            break;

        // Stop all sound output
        case SSTOP:
            for (i = 0; i < CH_TOTAL; ++i) {
                voice_stop(voice[i]);
            }
            break;

        default:
            break;
    }
}
