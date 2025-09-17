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
        size_t bytes_read = fread(vb_state->V810_GAME_RAM.pmemory + load_pos - rom_size, 1, chunk_size, load_sram);
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
        memcmp(tVBOpt.GAME_ID, "01VREE", 6) == 0 || // Red Alarm (U)
        memcmp(tVBOpt.GAME_ID, "E4VREJ", 6) == 0; // Red Alarm (J)
}

void predictEvent(bool increment) {
    if (increment) {
        vb_state->v810_state.cycles += vb_state->v810_state.cycles_until_event_full - vb_state->v810_state.cycles_until_event_partial;
    }

    WORD cycles = vb_state->v810_state.cycles;
    int disptime = cycles - vb_state->tVIPREG.lastdisp;
    int next_event = 400000 - disptime;
    if (vb_state->tVIPREG.displaying) {
        if (disptime < 60000) next_event = 60000 - disptime;
        else if (disptime < 160000) next_event = 160000 - disptime;
        else if (disptime < 200000) next_event = 200000 - disptime;
        else if (disptime < 260000) next_event = 260000 - disptime;
        else if (disptime < 360000) next_event = 360000 - disptime;
    }
    if (vb_state->tHReg.TCR & 0x01) {
        int ticks = vb_state->tHReg.tCount ? vb_state->tHReg.tCount : vb_state->tHReg.tTHW;
        if (!(vb_state->tHReg.TCR & 0x10)) ticks = ticks * 5 - vb_state->tHReg.ticks;
        int next_timer = ticks * 400 - (cycles - vb_state->tHReg.lasttime);
        if (next_event > next_timer) next_event = next_timer;
    }
    if (vb_state->tHReg.SCR & 2) {
        int next_input = vb_state->tHReg.hwRead - (cycles - vb_state->tHReg.lastinput);
        if (next_event > next_input) next_event = next_input;
    }
    if (vb_state->tVIPREG.drawing) {
        int drawtime = cycles - vb_state->tVIPREG.lastdraw;
        int sboff = (vb_state->tVIPREG.rowcount) * vb_state->tVIPREG.frametime / 28 + 1120;
        // the maths in serviceDisplayInt is slightly different, so add 1 to compensate
        int nextrow = (vb_state->tVIPREG.rowcount + 1) * vb_state->tVIPREG.frametime / 28 + 1;
        int next_draw;
        if (drawtime < sboff) next_draw = sboff - drawtime;
        else next_draw = nextrow - drawtime;
        if (next_event > next_draw) next_event = next_draw;
    }
    if (is_multiplayer) {
        int next_sync = vb_state->tHReg.lastsync + MULTIPLAYER_SYNC_CYCLES - cycles;
        if (next_event > next_sync) next_event = next_sync;
    }
    if (vb_state->tHReg.CCR & 0x04) {
        // communication underway
        int next_comm = vb_state->tHReg.nextcomm - cycles;
        if (next_event > next_comm) next_event = next_comm;
    }

    if (next_event < 0) next_event = 0;

    vb_state->v810_state.cycles_until_event_full = vb_state->v810_state.cycles_until_event_partial = next_event;
}

static int serviceDisplayInt(unsigned int cycles, WORD PC);

// Returns number of cycles until next timer interrupt.
int serviceInt(unsigned int cycles, WORD PC) {
    bool pending_int = false;

    // hardware read timing
    if (vb_state->tHReg.SCR & 2) {
        int next_input = vb_state->tHReg.hwRead - (cycles - vb_state->tHReg.lastinput);
        vb_state->tHReg.hwRead = next_input;
        if (next_input <= 0) {
            vb_state->tHReg.SCR &= ~2;
            pending_int = true;
        }
    }
    vb_state->tHReg.lastinput = cycles;

    // timer
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
    }

    // graphics has higher priority, so try that first
    pending_int = serviceDisplayInt(cycles, PC) || pending_int;

    // multiplayer stuff
    if (is_multiplayer) {
        if ((SWORD)(cycles - vb_state->tHReg.lastsync) >= MULTIPLAYER_SYNC_CYCLES) {
            vb_state->tHReg.lastsync += MULTIPLAYER_SYNC_CYCLES;
            vb_state->v810_state.ret = true;
            pending_int = true;
        }
    }

    if (vb_state->tHReg.CCR & 0x04) {
        // communication underway
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
        }
    }

    if (vb_state->tHReg.cInt || vb_state->tHReg.ccInt) {
        pending_int = v810_int(3, PC) || pending_int;
    }

    if (vb_state->tHReg.tInt) {
        // zero & interrupt enabled
        pending_int = v810_int(1, PC) || pending_int;
    }

    predictEvent(false);

    return pending_int;
}

static int serviceDisplayInt(unsigned int cycles, WORD PC) {
    int gamestart;
    unsigned int disptime = (cycles - vb_state->tVIPREG.lastdisp);
    bool pending_int = 0;

    if (unlikely(vb_state->tVIPREG.newframe)) {
        // new frame
        vb_state->tVIPREG.newframe = false;
        vb_state->tVIPREG.displaying = (vb_state->tVIPREG.DPCTRL & SYNCE) != 0;
        vb_state->tVIPREG.DPSTTS = (vb_state->tVIPREG.DPCTRL & (DISP|RE|SYNCE)) | SCANRDY | FCLK;

        int interrupts = FRAMESTART;

        if (!vb_state->tVIPREG.drawing) {
            vb_state->tVIPREG.lastdraw = vb_state->tVIPREG.lastdisp;
        }

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
                }
            }
        }

        vb_state->tVIPREG.INTPND |= interrupts;
        pending_int = 1;
    }

    // DPSTTS management
    {
        int dpstts_old = vb_state->tVIPREG.DPSTTS;
        int dpstts_new = dpstts_old;
        if (disptime >= 200000) {
            // FCLK low (high was handled already)
            dpstts_new &= ~FCLK;
        }
        if (likely(vb_state->tVIPREG.displaying)) {
            if (disptime < 60000) {
            } else if (disptime < 160000) {
                // LxBSY high
                dpstts_new |= vb_state->tVIPREG.tDisplayedFB & 1 ? L1BSY : L0BSY;
            } else if (disptime < 260000) {
                // LxBSY low
                dpstts_new &= ~DPBSY;
            } else if (disptime < 360000) {
                // RxBSY high
                dpstts_new |= vb_state->tVIPREG.tDisplayedFB & 1 ? R1BSY : R0BSY;
            } else {
                // RxBSY low
                dpstts_new &= ~DPBSY;
            }
        }
        if (unlikely(dpstts_new != dpstts_old)) {
            vb_state->tVIPREG.DPSTTS = dpstts_new;
            pending_int = 1;
            if (dpstts_old & DPBSY) {
                // old status had DPBSY, which necessarily means new one doesn't
                vb_state->tVIPREG.INTPND |= (dpstts_old & (L0BSY | L1BSY)) ? LFBEND : RFBEND;
            }
        }
    }

    unsigned int drawtime = (cycles-vb_state->tVIPREG.lastdraw);

    // XPSTTS management
    if (likely(vb_state->tVIPREG.drawing)) {
        int rowcount = drawtime * 28 / vb_state->tVIPREG.frametime;
        if (unlikely(rowcount > vb_state->tVIPREG.rowcount)) {
            pending_int = 1;
            if (rowcount < 28) {
                // new row mid-frame
                vb_state->tVIPREG.rowcount = rowcount;
                vb_state->tVIPREG.XPSTTS = (vb_state->tVIPREG.XPSTTS & 0xff) | (rowcount << 8) | SBOUT;
                // SBCMP comparison
                if (rowcount == ((vb_state->tVIPREG.XPCTRL >> 8) & 0x1f)) {
                    vb_state->tVIPREG.INTPND |= SBHIT;
                }
            } else {
                // finished drawing
                vb_state->tVIPREG.drawing = false;
                vb_state->tVIPREG.XPSTTS = 0x1b00 | (vb_state->tVIPREG.XPCTRL & XPEN);
                vb_state->tVIPREG.INTPND |= XPEND;
            }
        } else if (unlikely(rowcount < 28 && drawtime - rowcount * vb_state->tVIPREG.frametime / 28 >= 1120)) {
            // it's been roughly 56 microseconds, so clear SBOUT
            if (vb_state->tVIPREG.XPSTTS | SBOUT) pending_int = 1;
            vb_state->tVIPREG.XPSTTS &= ~SBOUT;
        }
    }

    if (unlikely(disptime >= 400000)) {
        // frame end
        vb_state->tVIPREG.rowcount = 0;
        vb_state->v810_state.ret = 1;
        vb_state->tVIPREG.lastdisp += 400000;
        vb_state->tVIPREG.newframe = true;
        pending_int = 1;

        if (vb_state->tVIPREG.tFrame == 0 && !vb_state->tVIPREG.drawing && (vb_state->tVIPREG.XPCTRL & XPEN)) {
            vb_state->tVIPREG.tDisplayedFB = !vb_state->tVIPREG.tDisplayedFB;
            if (!tVBOpt.VIP_OVERCLOCK) {
                vb_state->tVIPREG.frametime = videoProcessingTime();
            } else {
                // pre-0.9.7 behaviour
                vb_state->tVIPREG.frametime = 137216;
            }
        }

        sound_update(cycles);
    }

    if (unlikely(vb_state->tVIPREG.INTENB & vb_state->tVIPREG.INTPND)) {
        v810_int(4, PC);
        pending_int = 1;
    }

    predictEvent(false);

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

int v810_run(void) {
    vb_state->v810_state.ret = false;

    while (true) {
        int ret = 0;
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
