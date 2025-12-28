//////////////////////////////////////////////////////////
// Main CPU routines

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "minizip/unzip.h"

#include "v810_opt.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_types.h"
#include "vb_set.h"
#include "rom_db.h"
#include "drc_core.h"
#include "interpreter.h"
#include "vb_sound.h"
#include "vb_dsp.h"
#include "patches.h"

#include "replay.h"

#ifdef __3DS__
#include <3ds.h>
#endif

#define NEG(n) ((n) >> 31)
#define POS(n) ((~(n)) >> 31)

VB_STATE* vb_state;
VB_STATE vb_players[2];

#define MULTIPLAYER_SYNC_CYCLES 1000
bool is_multiplayer = false;
bool emulating_self = true;
int my_player_id = 0;
int emulated_player_id = 0;

V810_MEMORYFETCH V810_ROM1;

////////////////////////////////////////////////////////////
// Globals

const BYTE opcycle[0x50] = {
    0x01,0x01,0x01,0x01,0x01,0x01,0x03,0x01,0x0D,0x26,0x0D,0x24,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x0C,0x01,0x0F,0x0A,0x05,0x00,0x08,0x08,0x0C,0x00, //EI, HALT, LDSR, STSR, DI, BSTR -- Unknown clocks
    0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x01,0x01,0x03,0x03,0x01,0x01,0x01,0x01,
    0x04,0x04,0x0D,0x07,0x02,0x02,0x00,0x03,0x04,0x04,0x1A,0x07,0x02,0x02,0x00,0x03,
    0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x01,0x03,0x03
};

void v810_init(void) {
    char ram_name[32];
    unsigned int ram_size = 0;

    vb_state = &vb_players[0];
    memset(vb_state, 0, sizeof(*vb_state));

    V810_ROM1.pmemory = malloc(MAX_ROM_SIZE);
    // no backup because rom isn't volatile

    // Initialize our rom tables.... (USA)
    V810_ROM1.lowaddr  = 0x07000000;
    V810_ROM1.off = (size_t)V810_ROM1.pmemory - V810_ROM1.lowaddr;
    // Offset + Lowaddr = pmemory

    for (int i = 0; i < 2; i++) {
        // Initialize our ram1 tables....
        vb_players[i].V810_DISPLAY_RAM.lowaddr  = 0x00000000;
        vb_players[i].V810_DISPLAY_RAM.highaddr = 0x0003FFFF; //0x0005FFFF; //97FFF
        vb_players[i].V810_DISPLAY_RAM.size     = 0x00040000;
        // Alocate space for it in memory
        vb_players[i].V810_DISPLAY_RAM.pmemory = (unsigned char *)calloc(vb_players[i].V810_DISPLAY_RAM.size, sizeof(BYTE));
        // Offset + Lowaddr = pmemory
        vb_players[i].V810_DISPLAY_RAM.off = (size_t)vb_players[i].V810_DISPLAY_RAM.pmemory - vb_players[i].V810_DISPLAY_RAM.lowaddr;


        // Initialize our SoundRam tables....
        vb_players[i].V810_SOUND_RAM.lowaddr  = 0x01000000;
        vb_players[i].V810_SOUND_RAM.highaddr = 0x010007FF; //0x010002FF
        vb_players[i].V810_SOUND_RAM.size     = 0x00000800;
        // Alocate space for it in memory
        vb_players[i].V810_SOUND_RAM.pmemory = (unsigned char *)calloc(vb_players[i].V810_SOUND_RAM.size, sizeof(BYTE));
        // Offset + Lowaddr = pmemory
        vb_players[i].V810_SOUND_RAM.off = (size_t)vb_players[i].V810_SOUND_RAM.pmemory - vb_players[i].V810_SOUND_RAM.lowaddr;

        // Initialize our VBRam tables....
        vb_players[i].V810_VB_RAM.lowaddr  = 0x05000000;
        vb_players[i].V810_VB_RAM.highaddr = 0x0500FFFF;
        vb_players[i].V810_VB_RAM.size     = 0x00010000;
        // Alocate space for it in memory
        vb_players[i].V810_VB_RAM.pmemory = (unsigned char *)calloc(vb_players[i].V810_VB_RAM.size, sizeof(BYTE));
        // Offset + Lowaddr = pmemory
        vb_players[i].V810_VB_RAM.off = (size_t)vb_players[i].V810_VB_RAM.pmemory - vb_players[i].V810_VB_RAM.lowaddr;

        // Initialize our GameRam tables.... (Cartrige Ram)
        vb_players[i].V810_GAME_RAM.lowaddr  = 0x06000000;
        vb_players[i].V810_GAME_RAM.highaddr = 0x06003FFF; //0x06007FFF; //(8K, not 64k!)
        vb_players[i].V810_GAME_RAM.size     = 0x00004000;
        // Alocate space for it in memory
        vb_players[i].V810_GAME_RAM.pmemory = (unsigned char *)calloc(vb_players[i].V810_GAME_RAM.size, sizeof(BYTE));
        // Offset + Lowaddr = pmemory
        vb_players[i].V810_GAME_RAM.off = (size_t)vb_players[i].V810_GAME_RAM.pmemory - vb_players[i].V810_GAME_RAM.lowaddr;
    }
}

static bool load_is_zip;
static unzFile load_unz;
static FILE *load_file;
static size_t load_pos;
static FILE *load_sram;
static size_t load_sram_size;
int v810_load_init(void) {
    int rom_size;

    char *ext = strrchr(tVBOpt.ROM_PATH, '.');
    load_is_zip = ext != NULL && strcasecmp(ext, ".zip") == 0;
    load_pos = 0;

    if (load_is_zip) {
        load_unz = unzOpen(tVBOpt.ROM_PATH);
        if (load_unz == NULL) return -1;
        int err;
        if ((err = unzGoToFirstFile(load_unz)) != UNZ_OK) goto bail1;
        unz_file_info info;
        int filename_size = 256;
        char *filename = malloc(filename_size);
        while (true) {
            if ((err = unzGetCurrentFileInfo(load_unz, &info, filename, filename_size, NULL, 0, NULL, 0)) != UNZ_OK) goto bail2;
            if (info.size_filename > filename_size) {
                filename_size = info.size_filename + 1;
                filename = realloc(filename, filename_size);
                continue;
            }
            ext = strrchr(filename, '.');
            if (ext != NULL && strcasecmp(ext, ".vb") == 0) {
                // check filesize
                bool rom_size_valid = info.uncompressed_size >= 0x10 && info.uncompressed_size <= MAX_ROM_SIZE;
                // require po2
                rom_size_valid = rom_size_valid && !(info.uncompressed_size & (info.uncompressed_size - 1));
                if (rom_size_valid) {
                    if ((err = unzOpenCurrentFile(load_unz)) != UNZ_OK) goto bail2;
                    rom_size = info.uncompressed_size;
                    free(filename);
                    goto ok;
                }
            }
            if ((err = unzGoToNextFile(load_unz)) != UNZ_OK) goto bail2;
        }
        bail2:
        free(filename);
        bail1:
        unzClose(load_unz);
        return err;
    } else {
        // attempt to open as raw file
        load_file = fopen(tVBOpt.ROM_PATH, "rb");
        if (!load_file) return errno;
        struct stat mystat;
        fstat(fileno(load_file), &mystat);
        rom_size = mystat.st_size;
        bool rom_size_valid = rom_size >= 0x10 && rom_size <= MAX_ROM_SIZE;
        // require po2
        rom_size_valid = rom_size_valid && !(rom_size & (rom_size - 1));
        if (!rom_size_valid) {
            fclose(load_file);
            return EMSGSIZE;
        }
    }
    ok:
    V810_ROM1.size = rom_size;
    V810_ROM1.highaddr = 0x07000000 + rom_size - 1;

    load_sram = fopen(tVBOpt.RAM_PATH, "rb");
    if (load_sram) {
        struct stat mystat;
        fstat(fileno(load_sram), &mystat);
        load_sram_size = mystat.st_size;
        int max_sram_size = vb_state->V810_GAME_RAM.highaddr + 1 - vb_state->V810_GAME_RAM.lowaddr;
        if (load_sram_size > max_sram_size)
            load_sram_size = max_sram_size;
    } else {
        load_sram_size = 0;
    }

    load_pos = 0;
    return 0;
}

int v810_load_step(void) {
    int chunk_size = 0x10000;
    int rom_size = V810_ROM1.highaddr + 1 - V810_ROM1.lowaddr;
    int ram_size = load_sram_size;
    int all_size = rom_size + ram_size;
    if (load_pos < rom_size) {
        if (chunk_size > rom_size - load_pos)
            chunk_size = rom_size - load_pos;
        if (load_is_zip) {
            int ret = unzReadCurrentFile(load_unz, V810_ROM1.pmemory + load_pos, chunk_size);
            if (ret < 0) {
                unzCloseCurrentFile(load_unz);
                unzClose(load_unz);
                if (load_sram) fclose(load_sram);
                return ret;
            }
            load_pos += ret;
        } else {
            size_t bytes_read;
            if (!(bytes_read = fread(V810_ROM1.pmemory + load_pos, 1, chunk_size, load_file))) {
                fclose(load_file);
                if (load_sram) fclose(load_sram);
                return -ENOSPC;
            }
            load_pos += bytes_read;
        }
        if (load_pos >= rom_size) {
            // finish rom
            load_pos = rom_size;
            if (load_is_zip) {
                unzCloseCurrentFile(load_unz);
                unzClose(load_unz);
            } else {
                fclose(load_file);
            }
        }
    }
    if (load_pos >= rom_size && load_pos < all_size) {
        // load ram
        if (chunk_size > all_size - load_pos)
            chunk_size = all_size - load_pos;
        size_t bytes_read = fread(vb_players[0].V810_GAME_RAM.pmemory + load_pos - rom_size, 1, chunk_size, load_sram);
        if (bytes_read == 0 && !feof(load_sram)) {
            fclose(load_sram);
            return -ENETDOWN;
        }
        if ((load_pos += bytes_read) == all_size) {
            load_pos = all_size;
            fclose(load_sram);
        }
    }
    if (load_pos >= all_size) {
        // final setup

        // If we need to save, we'll find out later
        is_sram = false;

        // CRC32 Calculations
        gen_table();
        tVBOpt.CRC32 = get_crc(rom_size);
        memcpy(tVBOpt.GAME_ID, (char*)(V810_ROM1.off + (V810_ROM1.highaddr & 0xFFFFFDF9)), 6);

        // Apply game patches
        apply_patches();

        v810_reset();

        return 100;
    }
    return load_pos * 100 / all_size;
}

void v810_load_cancel(void) {
    is_sram = false;
    int rom_size = V810_ROM1.highaddr + 1 - V810_ROM1.lowaddr;
    int all_size = rom_size + load_sram_size;
    if (load_pos < rom_size) {
        if (load_is_zip) {
            unzCloseCurrentFile(load_unz);
            unzClose(load_unz);
        } else {
            fclose(load_file);
        }
    } else if (load_pos < all_size) {
        if (load_sram) fclose(load_sram);
    }
}

void v810_exit(void) {
    free(V810_ROM1.pmemory);
    for (int i = 0; i < 2; i++) {
        free(vb_players[i].V810_DISPLAY_RAM.pmemory);
        free(vb_players[i].V810_SOUND_RAM.pmemory);
        free(vb_players[i].V810_VB_RAM.pmemory);
        free(vb_players[i].V810_GAME_RAM.pmemory);
    }
}

// Reinitialize the defaults in the CPU
void v810_reset(void) {
    for (int i = 0; i < 2; i++) {
        vb_state = &vb_players[i];

        memset(&vb_state->v810_state, 0, sizeof(vb_state->v810_state));
        memset(&vb_state->tVIPREG, 0, sizeof(vb_state->tVIPREG));
        memset(&vb_state->tHReg, 0, sizeof(vb_state->tHReg));

        vb_state->tVIPREG.newframe = true;

        vb_state->v810_state.irq_handler = &drc_handleInterrupts;
        vb_state->v810_state.reloc_table = &drc_relocTable;

        vb_state->v810_state.next_event_type = EVENT_DISPLAY;
        for (int i = 0; i < EVENT_COUNT; i++) {
            if (i != EVENT_DISPLAY) {
                vb_state->v810_state.event_timestamps[i] = INT32_MAX;
            }
        }

        vb_state->v810_state.P_REG[0]    =  0x00000000;
        vb_state->v810_state.PC          =  0xFFFFFFF0;
        vb_state->v810_state.S_REG[ECR]  =  0x0000FFF0;
        vb_state->v810_state.S_REG[PSW]  =  0x00008000;
        vb_state->v810_state.S_REG[PIR]  =  0x00005346;
        vb_state->v810_state.S_REG[TKCW] =  0x000000E0;

        vb_state->tHReg.SCR = 0;
        vb_state->tHReg.TCR = 0;
        vb_state->tHReg.WCR = 0;
        vb_state->tVIPREG.INTENB = 0;
        vb_state->tVIPREG.XPSTTS &= ~2;

        mem_whword(0x0005F840, 0x0004); //XPSTTS

        vb_state->tHReg.SCR	= 0x4C;
        vb_state->tHReg.WCR	= 0xFC;
        vb_state->tHReg.TCR	= 0xE4;
        vb_state->tHReg.THB	= 0xFF;
        vb_state->tHReg.TLB	= 0xFF;
        vb_state->tHReg.SHB	= 0x00;
        vb_state->tHReg.SLB	= 0x00;
        vb_state->tHReg.CDRR	= 0x00;
        vb_state->tHReg.CDTR	= 0x00;
        vb_state->tHReg.CCSR	= 0xFA;
        vb_state->tHReg.CCR	= 0x69;

        vb_state->tHReg.tCount = 0xFFFF;

        vb_state->tHReg.hwRead = 0;
    }
    
    emulated_player_id = 0;
    emulating_self = emulated_player_id == my_player_id;
    vb_state = &vb_players[emulated_player_id];

    // we don't reset load_sram so it will be non-null if there was sram to load
    replay_reset(is_sram || (bool)load_sram);

    // Golf might set this to RM_CPUONLY, so reset it here.
    tVBOpt.RENDERMODE = RM_TOGPU;

    // Software rendering for Test Chamber, only on New 3DS.
    if (memcmp(tVBOpt.GAME_ID, "PRCHMB", 6) == 0) {
        bool new_3ds = true;
        #ifdef __3DS__
        APT_CheckNew3DS(&new_3ds);
        #endif
        if (new_3ds) tVBOpt.RENDERMODE = RM_CPUONLY;
    }

    tVBOpt.VIP_OVER_SOFT = (
        memcmp(tVBOpt.GAME_ID, "01VREE", 6) == 0 // Red Alarm (U)
        || memcmp(tVBOpt.GAME_ID, "E4VREJ", 6) == 0 // Red Alarm (J)
    );

    // Double buffering is more accurate, but adds 1 frame of input lag.
    tVBOpt.DOUBLE_BUFFER =
        memcmp(tVBOpt.GAME_ID, "PRCHMB", 6) == 0 || // Test Chamber
        memcmp(tVBOpt.GAME_ID, "01VBHE", 6) == 0 || // Bound High
        memcmp(tVBOpt.GAME_ID, "EBVJBE", 6) == 0 || // Jack Bros. (U)
        memcmp(tVBOpt.GAME_ID, "EBVJBJ", 6) == 0 || // Jack Bros. (J)
        memcmp(tVBOpt.GAME_ID, "01VREE", 6) == 0 || // Red Alarm (U)
        memcmp(tVBOpt.GAME_ID, "E4VREJ", 6) == 0; // Red Alarm (J)
}

void updatePrediction(VB_STATE *vb_state, EventType event_type, bool increment) {
    if (event_type == EVENT_TIMER) {
        if (vb_state->tHReg.TCR & 0x01) {
            int ticks = vb_state->tHReg.tCount ? vb_state->tHReg.tCount : vb_state->tHReg.tTHW;
            if (!(vb_state->tHReg.TCR & 0x10)) ticks = ticks * 5 - vb_state->tHReg.ticks;
            vb_state->v810_state.event_timestamps[EVENT_TIMER] = ticks * 400 + vb_state->tHReg.lasttime;
        } else {
            vb_state->v810_state.event_timestamps[EVENT_TIMER] = INT32_MAX;
        }
    } else if (event_type == EVENT_COMM) {
        if (!is_multiplayer && (vb_state->tHReg.CCR & 0x14) == 0x14) {
            // single player remote comm should never finish
            vb_state->v810_state.event_timestamps[EVENT_COMM] = INT32_MAX;
        } else if (vb_state->tHReg.CCR & 0x04) {
            vb_state->v810_state.event_timestamps[EVENT_COMM] = vb_state->tHReg.nextcomm;
        } else {
            vb_state->v810_state.event_timestamps[EVENT_COMM] = INT32_MAX;
        }
    } else if (event_type == EVENT_INPUT) {
        if (vb_state->tHReg.SCR & 0x02) {
            vb_state->v810_state.event_timestamps[EVENT_INPUT] = vb_state->tHReg.hwRead + vb_state->tHReg.lastinput;
        } else {
            vb_state->v810_state.event_timestamps[EVENT_INPUT] = INT32_MAX;
        }
    } else if (event_type == EVENT_SYNC) {
        vb_state->v810_state.event_timestamps[EVENT_SYNC] = vb_state->tHReg.lastsync + MULTIPLAYER_SYNC_CYCLES;
    }
    predictEvent(vb_state, increment);
}

void predictEvent(VB_STATE *vb_state, bool increment) {
    if (increment) {
        vb_state->v810_state.cycles += vb_state->v810_state.cycles_until_event_full - vb_state->v810_state.cycles_until_event_partial;
    }

    EventType next_event_type = 0;
    int next_event = vb_state->v810_state.event_timestamps[0];
    for (int i = 1; i < EVENT_COUNT; i++) {
        if (next_event > vb_state->v810_state.event_timestamps[i]) {
            next_event = vb_state->v810_state.event_timestamps[i];
            next_event_type = i;
        }
    }

    vb_state->v810_state.next_event_type = next_event_type;
    next_event -= vb_state->v810_state.cycles;

    if (next_event < 0) next_event = 0;

    vb_state->v810_state.cycles_until_event_full = vb_state->v810_state.cycles_until_event_partial = next_event;
}

bool checkInterrupts(WORD PC) {
    int interrupt = 0;
    if (unlikely(vb_state->tVIPREG.INTENB & vb_state->tVIPREG.INTPND)) {
        interrupt = 4;
    } else if (unlikely(vb_state->tHReg.cInt || vb_state->tHReg.ccInt)) {
        interrupt = 3;
    } else if (unlikely(vb_state->tHReg.tInt)) {
        interrupt = 1;
    }

    return interrupt != 0 && v810_int(interrupt, PC);
}

static bool eventInput(int cycles, WORD PC) {
    bool pending_int = false;
    if (vb_state->tHReg.SCR & 2) {
        int next_input = vb_state->tHReg.hwRead - (cycles - vb_state->tHReg.lastinput);
        vb_state->tHReg.hwRead = next_input;
        if (next_input <= 0) {
            vb_state->tHReg.SCR &= ~2;
            pending_int = true;
            updatePrediction(vb_state, EVENT_INPUT, false);
        }
    }
    vb_state->tHReg.lastinput = cycles;
    return pending_int;
}

static bool eventTimer(int cycles, WORD PC) {
    if ((cycles-vb_state->tHReg.lasttime) >= 400) {
        int new_ticks = (cycles - vb_state->tHReg.lasttime) / 400;
        vb_state->tHReg.lasttime += 400 * new_ticks;
        int steps = (vb_state->tHReg.TCR & 0x10) ? new_ticks : (vb_state->tHReg.ticks + new_ticks) / 5;
        vb_state->tHReg.ticks = (vb_state->tHReg.ticks + new_ticks) % 5;
        if (vb_state->tHReg.TCR & 0x01) { // Timer Enabled
            vb_state->tHReg.tCount -= steps;
            if (vb_state->tHReg.tCount <= 0 && vb_state->tHReg.tCount + steps > 0) {
                vb_state->tHReg.TCR |= 0x02;
                if ((vb_state->tHReg.TCR & 0x09) == 0x09) vb_state->tHReg.tInt = true;
            }
            while (vb_state->tHReg.tCount < 0) {
                vb_state->tHReg.tCount += vb_state->tHReg.tTHW + 1; //reset counter
            }
            vb_state->tHReg.TLB = (vb_state->tHReg.tCount&0xFF);
            vb_state->tHReg.THB = ((vb_state->tHReg.tCount>>8)&0xFF);
        }
        updatePrediction(vb_state, EVENT_TIMER, false);
    }
    return false;
}

static bool eventSync(int cycles, WORD PC) {
    if (is_multiplayer && (SWORD)(cycles - vb_state->tHReg.lastsync) >= MULTIPLAYER_SYNC_CYCLES) {
        vb_state->tHReg.lastsync += MULTIPLAYER_SYNC_CYCLES;
        updatePrediction(vb_state, EVENT_SYNC, false);
        vb_state->v810_state.ret = true;
        return true;
    }
    return false;
}

static bool eventComm(int cycles, WORD PC) {
    if (vb_state->tHReg.CCR & 0x04) {
        // communication underway
        if (!is_multiplayer && (vb_state->tHReg.CCR & 0x14) == 0x14) {
            // single player remote comm should never finish
            vb_state->tHReg.nextcomm = INT32_MAX;
            updatePrediction(vb_state, EVENT_COMM, false);
        }
        if ((SWORD)(vb_state->tHReg.nextcomm - cycles) <= 0) {
            // communication complete
            vb_state->tHReg.cLock = false;
            vb_state->tHReg.CCR &= ~0x06;
            if (!(vb_state->tHReg.CCR & 0x80)) vb_state->tHReg.cInt = true;
            if (is_multiplayer) {
                vb_state->tHReg.CCSR = (vb_state->tHReg.CCSR & ~0x04) | (((vb_players[0].tHReg.CCSR & 0x0A) == 0x0A && (vb_players[1].tHReg.CCSR & 0x0A) == 0x0A) << 2);
            } else {
                vb_state->tHReg.CCSR = vb_state->tHReg.CCSR | 0x04;
            }
            if (!(vb_state->tHReg.CCSR & 0x80) && ((vb_state->tHReg.CCSR & 0x14) == 0x14 || (vb_state->tHReg.CCSR & 0x14) == 0)) {
                vb_state->tHReg.ccInt = true;
            }
            updatePrediction(vb_state, EVENT_COMM, false);
        }
    }
    return false;
}

static bool eventDisplay(int cycles, WORD PC) {
    int gamestart;
    unsigned int disptime = (cycles - vb_state->tVIPREG.lastdisp);
    bool pending_int = false;

    if (unlikely(vb_state->tVIPREG.newframe)) {
        // new frame
        vb_state->tVIPREG.newframe = false;
        vb_state->tVIPREG.displaying = (vb_state->tVIPREG.DPCTRL & SYNCE) != 0;
        vb_state->tVIPREG.DPSTTS = (vb_state->tVIPREG.DPCTRL & (DISP|RE|SYNCE)) | SCANRDY | FCLK;

        int interrupts = FRAMESTART;

        if (!vb_state->tVIPREG.drawing) {
            vb_state->tVIPREG.lastdraw = vb_state->tVIPREG.lastdisp;
        }

        vb_state->v810_state.event_timestamps[EVENT_DISPLAY] = vb_state->tVIPREG.displaying ? 60000 : 200000;

        if (vb_state->tVIPREG.tFrame-- == 0) {
            vb_state->tVIPREG.tFrame = vb_state->tVIPREG.FRMCYC;
            interrupts |= GAMESTART;
            if (vb_state->tVIPREG.XPCTRL & XPEN) {
                if (vb_state->tVIPREG.drawing) {
                    vb_state->tVIPREG.XPSTTS |= OVERTIME;
                    interrupts |= TIMEERR;
                } else {
                    vb_state->tVIPREG.drawing = true;
                    vb_state->tVIPREG.XPSTTS = XPEN | ((!vb_state->tVIPREG.tDisplayedFB+1)<<2) | SBOUT;
                    vb_state->tVIPREG.rowcount = 0;
                    vb_state->v810_state.event_timestamps[EVENT_DRAW] = vb_state->tVIPREG.frametime / 28 + 1;
                }
            }
        }

        updatePrediction(vb_state, EVENT_DISPLAY, false);

        vb_state->tVIPREG.INTPND |= interrupts;
        pending_int = true;
    }

    // DPSTTS management
    {
        int dpstts_old = vb_state->tVIPREG.DPSTTS;
        int dpstts_new = dpstts_old;
        int next_event = vb_state->v810_state.event_timestamps[EVENT_DISPLAY];
        if (disptime >= 200000) {
            // FCLK low (high was handled already)
            dpstts_new &= ~FCLK;
            next_event = vb_state->tVIPREG.displaying ? 260000 : 400000;
        }
        if (likely(vb_state->tVIPREG.displaying)) {
            if (disptime < 60000) {
            } else if (disptime < 160000) {
                // LxBSY high
                dpstts_new |= vb_state->tVIPREG.tDisplayedFB & 1 ? L1BSY : L0BSY;
                next_event = 160000;
            } else if (disptime < 200000) {
                // LxBSY low
                dpstts_new &= ~DPBSY;
                next_event = 200000;
            } else if (disptime < 260000) {
            } else if (disptime < 360000) {
                // RxBSY high
                dpstts_new |= vb_state->tVIPREG.tDisplayedFB & 1 ? R1BSY : R0BSY;
                next_event = 360000;
            } else {
                // RxBSY low
                dpstts_new &= ~DPBSY;
                next_event = 400000;
            }
        }
        if (unlikely(next_event != vb_state->v810_state.event_timestamps[EVENT_DISPLAY])) {
            vb_state->v810_state.event_timestamps[EVENT_DISPLAY] = next_event;
            updatePrediction(vb_state, EVENT_DISPLAY, false);
        }
        if (unlikely(dpstts_new != dpstts_old)) {
            vb_state->tVIPREG.DPSTTS = dpstts_new;
            pending_int = true;
            if (dpstts_old & DPBSY) {
                // old status had DPBSY, which necessarily means new one doesn't
                vb_state->tVIPREG.INTPND |= (dpstts_old & (L0BSY | L1BSY)) ? LFBEND : RFBEND;
            }
        }
    }

    if (unlikely(disptime >= 400000)) {
        // frame end
        vb_state->tVIPREG.rowcount = 0;
        vb_state->v810_state.ret = 1;
        vb_state->tVIPREG.lastdisp += 400000;
        vb_state->tVIPREG.newframe = true;
        pending_int = true;

        if (vb_state->tVIPREG.tFrame == 0 && !vb_state->tVIPREG.drawing && (vb_state->tVIPREG.XPCTRL & XPEN)) {
            vb_state->tVIPREG.tDisplayedFB = !vb_state->tVIPREG.tDisplayedFB;
            if (!tVBOpt.VIP_OVERCLOCK || is_multiplayer) {
                vb_state->tVIPREG.frametime = videoProcessingTime();
            } else {
                // pre-0.9.7 behaviour
                vb_state->tVIPREG.frametime = 137216;
            }
        }

        sound_update(cycles);
    }

    return pending_int;
}

static bool eventDraw(int cycles, WORD PC) {
    bool pending_int = false;

    unsigned int drawtime = (cycles-vb_state->tVIPREG.lastdraw);

    // XPSTTS management
    if (likely(vb_state->tVIPREG.drawing)) {
        int next_event = vb_state->v810_state.event_timestamps[EVENT_DRAW];
        int rowcount = drawtime * 28 / vb_state->tVIPREG.frametime;
        if (unlikely(rowcount > vb_state->tVIPREG.rowcount)) {
            pending_int = true;
            if (rowcount < 28) {
                // new row mid-frame
                vb_state->tVIPREG.rowcount = rowcount;
                vb_state->tVIPREG.XPSTTS = (vb_state->tVIPREG.XPSTTS & 0xff) | (rowcount << 8) | SBOUT;
                // SBCMP comparison
                if (rowcount == ((vb_state->tVIPREG.XPCTRL >> 8) & 0x1f)) {
                    vb_state->tVIPREG.INTPND |= SBHIT;
                }
                next_event = vb_state->tVIPREG.lastdraw + rowcount * vb_state->tVIPREG.frametime / 28 + 1120;
            } else {
                // finished drawing
                vb_state->tVIPREG.drawing = false;
                vb_state->tVIPREG.XPSTTS = 0x1b00 | (vb_state->tVIPREG.XPCTRL & XPEN);
                vb_state->tVIPREG.INTPND |= XPEND;
                next_event = INT_MAX;
            }
        } else if (unlikely(rowcount < 28 && drawtime - rowcount * vb_state->tVIPREG.frametime / 28 >= 1120)) {
            // it's been roughly 56 microseconds, so clear SBOUT
            if (vb_state->tVIPREG.XPSTTS | SBOUT) pending_int = true;
            vb_state->tVIPREG.XPSTTS &= ~SBOUT;
            next_event = vb_state->tVIPREG.lastdraw + (rowcount + 1) * vb_state->tVIPREG.frametime / 28 + 1;
        }
        if (next_event != vb_state->v810_state.event_timestamps[EVENT_DRAW]) {
            vb_state->v810_state.event_timestamps[EVENT_DRAW] = next_event;
            updatePrediction(vb_state, EVENT_DRAW, false);
        }
    }

    return pending_int;
}

int serviceInt(int cycles, WORD PC) {
    bool pending_int = false;

    for (int i = 0; i < EVENT_COUNT; i++) {
        switch (i) {
            case EVENT_INPUT  : pending_int = eventInput  (cycles, PC) || pending_int; break;
            case EVENT_DISPLAY: pending_int = eventDisplay(cycles, PC) || pending_int; break;
            case EVENT_DRAW   : pending_int = eventDraw   (cycles, PC) || pending_int; break;
            case EVENT_TIMER  : pending_int = eventTimer  (cycles, PC) || pending_int; break;
            case EVENT_SYNC   : pending_int = eventSync   (cycles, PC) || pending_int; break;
            case EVENT_COMM   : pending_int = eventComm   (cycles, PC) || pending_int; break;
            case EVENT_COUNT: break; // unreachable
        }
    }

    pending_int = checkInterrupts(PC) || pending_int;

    return pending_int;
}

// Generate Interupt #n
bool v810_int(WORD iNum, WORD PC) {
    if (iNum > 0x0F) return false;  // Invalid Interupt number...
    if((vb_state->v810_state.S_REG[PSW] & PSW_NP)) return false;
    if((vb_state->v810_state.S_REG[PSW] & PSW_EP)) return false; // Exception pending?
    if((vb_state->v810_state.S_REG[PSW] & PSW_ID)) return false; // Interupt disabled
    if(iNum < ((vb_state->v810_state.S_REG[PSW] & PSW_IA)>>16)) return false; // Interupt to low on the chain

    // if an interrupt happened, skip a HALT instruction if we're on one
    if (((HWORD)mem_rhword(PC) >> 10) == V810_OP_HALT) {
        PC += 2;
    }

    //Ready to Generate the Interupts
    vb_state->v810_state.S_REG[EIPC]  = PC;
    vb_state->v810_state.S_REG[EIPSW] = vb_state->v810_state.S_REG[PSW];
    vb_state->v810_state.except_flags = vb_state->v810_state.flags;

    vb_state->v810_state.PC = 0xFFFFFE00 | (iNum << 4);

    vb_state->v810_state.S_REG[ECR] = 0xFE00 | (iNum << 4);
    vb_state->v810_state.S_REG[PSW] = vb_state->v810_state.S_REG[PSW] | PSW_EP;
    vb_state->v810_state.S_REG[PSW] = vb_state->v810_state.S_REG[PSW] | PSW_ID;
    if((iNum+=1) > 0x0F)
        (iNum = 0x0F);
    vb_state->v810_state.S_REG[PSW] = vb_state->v810_state.S_REG[PSW] | (iNum << 16); //Set the Interupt
    return true;
}

// Generate exception #n
// Exceptions are Div by zero, trap and Invalid Opcode, we can live without...
void v810_exp(WORD iNum, WORD eCode) {
    if (iNum > 0x0F) return;  // Invalid Exception number...

    //if(!S_REG[PSW]&PSW_ID) return;
    //if(iNum < ((S_REG[PSW] & PSW_IA)>>16)) return; // Interupt to low on the mask level....
    if ((vb_state->v810_state.S_REG[PSW] & PSW_IA)>>16) return; //Interrupt Pending

    eCode &= 0xFFFF;

    if(vb_state->v810_state.S_REG[PSW]&PSW_EP) { //Double Exception
        vb_state->v810_state.S_REG[FEPC] = vb_state->v810_state.PC;
        vb_state->v810_state.S_REG[FEPSW] = vb_state->v810_state.S_REG[PSW];
        vb_state->v810_state.S_REG[ECR] = (eCode << 16); //Exception Code, dont get it???
        vb_state->v810_state.S_REG[PSW] = vb_state->v810_state.S_REG[PSW] | PSW_NP;
        vb_state->v810_state.S_REG[PSW] = vb_state->v810_state.S_REG[PSW] | PSW_ID;
        //S_REG[PSW] = S_REG[PSW] | (((iNum+1) & 0x0f) << 16); //Set the Interupt status

        vb_state->v810_state.PC = 0xFFFFFFD0;
        return;
    } else { // Regular Exception
        vb_state->v810_state.S_REG[EIPC] = vb_state->v810_state.PC;
        vb_state->v810_state.S_REG[EIPSW] = vb_state->v810_state.S_REG[PSW];
        vb_state->v810_state.except_flags = vb_state->v810_state.flags;
        vb_state->v810_state.S_REG[ECR] = eCode; //Exception Code, dont get it???
        vb_state->v810_state.S_REG[PSW] = vb_state->v810_state.S_REG[PSW] | PSW_EP;
        vb_state->v810_state.S_REG[PSW] = vb_state->v810_state.S_REG[PSW] | PSW_ID;
        //S_REG[PSW] = S_REG[PSW] | (((iNum+1) & 0x0f) << 16); //Set the Interupt status

        vb_state->v810_state.PC = 0xFFFFFF00 | (iNum << 4);
        return;
    }
}

void v810_reset_timings(void) {
    #ifdef __3DS__
    sound_state.last_cycles -= vb_players[my_player_id].tVIPREG.lastdisp;
    #endif
    for (int i = 0; i < 2; i++) {
        int off = vb_players[i].tVIPREG.lastdisp;
        vb_players[i].v810_state.cycles -= off;
        vb_players[i].tVIPREG.lastdraw -= off;
        if (vb_players[i].tHReg.SCR & 2) {
            vb_players[i].tHReg.lastinput -= off;
        }
        vb_players[i].tHReg.lastsync -= off;
        vb_players[i].tHReg.lasttime -= off;
        if (vb_players[i].tHReg.nextcomm != INT32_MAX)
            vb_players[i].tHReg.nextcomm -= off;
        for (int j = 0; j < EVENT_COUNT; j++) {
            if (vb_players[i].v810_state.event_timestamps[j] != INT32_MAX) {
                vb_players[i].v810_state.event_timestamps[j] -= off;
            }
        }
        vb_players[i].tVIPREG.lastdisp = 0;
        if (is_multiplayer) {
            updatePrediction(&vb_players[i], EVENT_SYNC, false);
        }
    }
}

int v810_run(void) {
    vb_state->v810_state.ret = false;

    v810_reset_timings();

    while (true) {
        int ret = 0;
        if (is_multiplayer) updatePrediction(vb_state, EVENT_SYNC, false);
        #if DRC_AVAILABLE
        if (likely((vb_state->v810_state.PC & 0x07000000) == 0x07000000)) {
            ret = drc_run();
        } else
        #endif
        {
            ret = interpreter_run();
        }
        if (ret != 0) return ret;
        if (vb_state->v810_state.ret) {
            vb_state->v810_state.ret = false;
            if (is_multiplayer) {
                emulated_player_id ^= 1;
                vb_state = &vb_players[emulated_player_id];
                emulating_self = emulated_player_id == my_player_id;

                if(emulated_player_id == 0) {
                    // sync things
                    for (int i = 0; i < 2; i++) {
                        // am i in communication using an external clock?
                        if ((vb_players[i].tHReg.CCR & 0x14) == 0x14) {
                            // is the other one in communication using its internal clock?
                            if ((vb_players[!i].tHReg.CCR & 0x14) == 0x04) {
                                // communication happening, swap the data
                                vb_players[i].tHReg.nextcomm = vb_players[!i].tHReg.nextcomm;
                                vb_players[i].tHReg.CDRR = vb_players[!i].tHReg.CDTR;
                                vb_players[!i].tHReg.CDRR = vb_players[i].tHReg.CDTR;
                                vb_players[i].tHReg.cLock = true;
                                vb_players[!i].tHReg.cLock = true;
                            } else if (!vb_players[i].tHReg.cLock) {
                                // waiting on nothing, so delay until after the next sync
                                vb_players[i].tHReg.nextcomm = vb_players[i].v810_state.cycles + 8000;
                            }
                            updatePrediction(&vb_players[i], EVENT_COMM, false);
                        }
                    }
                }
            }
            if (emulated_player_id == 0 && vb_state->tVIPREG.newframe) {
                break;
            }
        }
    }

    return 0;
}

void v810_endmultiplayer() {
    if (my_player_id == 1) {
        VB_STATE tmp;
        memcpy(&tmp, &vb_players[0], sizeof(tmp));
        memcpy(&vb_players[0], &vb_players[1], sizeof(tmp));
        memcpy(&vb_players[1], &tmp, sizeof(tmp));
    }
    my_player_id = 0;
    emulated_player_id = 0;
    emulating_self = true;
    is_multiplayer = false;
    vb_players[0].v810_state.event_timestamps[EVENT_SYNC] = INT32_MAX;
    updatePrediction(vb_state, EVENT_SYNC, false);
}
