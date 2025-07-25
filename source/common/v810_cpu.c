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

#define NEG(n) ((n) >> 31)
#define POS(n) ((~(n)) >> 31)

cpu_state* v810_state;

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

    V810_ROM1.pmemory = malloc(MAX_ROM_SIZE);
    // no backup because rom isn't volatile

    // Initialize our rom tables.... (USA)
    V810_ROM1.lowaddr  = 0x07000000;
    V810_ROM1.off = (size_t)V810_ROM1.pmemory - V810_ROM1.lowaddr;
    // Offset + Lowaddr = pmemory

    // Initialize our ram1 tables....
    V810_DISPLAY_RAM.lowaddr  = 0x00000000;
    V810_DISPLAY_RAM.highaddr = 0x0003FFFF; //0x0005FFFF; //97FFF
    V810_DISPLAY_RAM.size     = 0x00040000;
    // Alocate space for it in memory
    V810_DISPLAY_RAM.pmemory = (unsigned char *)calloc(((V810_DISPLAY_RAM.highaddr +1) - V810_DISPLAY_RAM.lowaddr), sizeof(BYTE));
    V810_DISPLAY_RAM.pbackup = (unsigned char *)calloc(((V810_DISPLAY_RAM.highaddr +1) - V810_DISPLAY_RAM.lowaddr), sizeof(BYTE));
    // Offset + Lowaddr = pmemory
    V810_DISPLAY_RAM.off = (size_t)V810_DISPLAY_RAM.pmemory - V810_DISPLAY_RAM.lowaddr;

    // Initialize our VIPC Reg tables....
    V810_VIPCREG.lowaddr  = 0x0005F800; //0x0005F800
    V810_VIPCREG.highaddr = 0x0005FFFF; //0x0005F870
    // Point to the handler funcs...
    V810_VIPCREG.rfuncb = &(vipcreg_rbyte);
    V810_VIPCREG.wfuncb = &(vipcreg_wbyte);
    V810_VIPCREG.rfunch = &(vipcreg_rhword);
    V810_VIPCREG.wfunch = &(vipcreg_whword);
    V810_VIPCREG.rfuncw = &(vipcreg_rword);
    V810_VIPCREG.wfuncw = &(vipcreg_wword);

    // Initialize our SoundRam tables....
    V810_SOUND_RAM.lowaddr  = 0x01000000;
    V810_SOUND_RAM.highaddr = 0x010007FF; //0x010002FF
    V810_SOUND_RAM.size     = 0x00000800;
    // Alocate space for it in memory
    V810_SOUND_RAM.pmemory = (unsigned char *)calloc(((V810_SOUND_RAM.highaddr +1) - V810_SOUND_RAM.lowaddr), sizeof(BYTE));
    V810_SOUND_RAM.pbackup = (unsigned char *)calloc(((V810_SOUND_RAM.highaddr +1) - V810_SOUND_RAM.lowaddr), sizeof(BYTE));
    // Offset + Lowaddr = pmemory
    V810_SOUND_RAM.off = (size_t)V810_SOUND_RAM.pmemory - V810_SOUND_RAM.lowaddr;

    // Initialize our VBRam tables....
    V810_VB_RAM.lowaddr  = 0x05000000;
    V810_VB_RAM.highaddr = 0x0500FFFF;
    V810_VB_RAM.size     = 0x00010000;
    // Alocate space for it in memory
    V810_VB_RAM.pmemory = (unsigned char *)calloc(((V810_VB_RAM.highaddr +1) - V810_VB_RAM.lowaddr), sizeof(BYTE));
    V810_VB_RAM.pbackup = (unsigned char *)calloc(((V810_VB_RAM.highaddr +1) - V810_VB_RAM.lowaddr), sizeof(BYTE));
    // Offset + Lowaddr = pmemory
    V810_VB_RAM.off = (size_t)V810_VB_RAM.pmemory - V810_VB_RAM.lowaddr;

    // Initialize our GameRam tables.... (Cartrige Ram)
    V810_GAME_RAM.lowaddr  = 0x06000000;
    V810_GAME_RAM.highaddr = 0x06003FFF; //0x06007FFF; //(8K, not 64k!)
    V810_GAME_RAM.size     = 0x00004000;
    // Alocate space for it in memory
    V810_GAME_RAM.pmemory = (unsigned char *)calloc(((V810_GAME_RAM.highaddr +1) - V810_GAME_RAM.lowaddr), sizeof(BYTE));
    V810_GAME_RAM.pbackup = (unsigned char *)calloc(((V810_GAME_RAM.highaddr +1) - V810_GAME_RAM.lowaddr), sizeof(BYTE));
    // Offset + Lowaddr = pmemory
    V810_GAME_RAM.off = (size_t)V810_GAME_RAM.pmemory - V810_GAME_RAM.lowaddr;

    // Initialize our HCREG tables.... // realy reg01
    V810_HCREG.lowaddr  = 0x02000000;
    V810_HCREG.highaddr = 0x02FFFFFF; // Realy just 0200002C but its mirrored...
    // Point to the handler funcs...
    V810_HCREG.rfuncb = &(hcreg_rbyte);
    V810_HCREG.wfuncb = &(hcreg_wbyte);
    V810_HCREG.rfunch = &(hcreg_rhword);
    V810_HCREG.wfunch = &(hcreg_whword);
    V810_HCREG.rfuncw = &(hcreg_rword);
    V810_HCREG.wfuncw = &(hcreg_wword);

    v810_state = calloc(1, sizeof(cpu_state));
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
        int max_sram_size = V810_GAME_RAM.highaddr + 1 - V810_GAME_RAM.lowaddr;
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
        size_t bytes_read = fread(V810_GAME_RAM.pmemory + load_pos - rom_size, 1, chunk_size, load_sram);
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
    free(v810_state);
    free(V810_ROM1.pmemory);
    free(V810_DISPLAY_RAM.pmemory);
    free(V810_SOUND_RAM.pmemory);
    free(V810_VB_RAM.pmemory);
    free(V810_GAME_RAM.pmemory);
}

// Reinitialize the defaults in the CPU
void v810_reset(void) {
    memset(v810_state, 0, sizeof(cpu_state));
    memset(&tVIPREG, 0, sizeof(tVIPREG));
    memset(&tHReg, 0, sizeof(tHReg));

    tVIPREG.newframe = true;

    v810_state->irq_handler = &drc_handleInterrupts;
    v810_state->reloc_table = &drc_relocTable;

    v810_state->P_REG[0]    =  0x00000000;
    v810_state->PC          =  0xFFFFFFF0;
    v810_state->S_REG[ECR]  =  0x0000FFF0;
    v810_state->S_REG[PSW]  =  0x00008000;
    v810_state->S_REG[PIR]  =  0x00005346;
    v810_state->S_REG[TKCW] =  0x000000E0;

    tHReg.SCR = 0;
    tHReg.TCR = 0;
    tHReg.WCR = 0;
    tVIPREG.INTENB = 0;
    tVIPREG.XPSTTS &= ~2;

    mem_whword(0x0005F840, 0x0004); //XPSTTS

    tHReg.SCR	= 0x4C;
    tHReg.WCR	= 0xFC;
    tHReg.TCR	= 0xE4;
    tHReg.THB	= 0xFF;
    tHReg.TLB	= 0xFF;
    tHReg.SHB	= 0x00;
    tHReg.SLB	= 0x00;
    tHReg.CDRR	= 0x00;
    tHReg.CDTR	= 0x00;
    tHReg.CCSR	= 0xFF;
    tHReg.CCR	= 0x6D;

    tHReg.tCount = 0xFFFF;

    tHReg.hwRead = 0;

    // we don't reset load_sram so it will be non-null if there was sram to load
    replay_reset(is_sram || (bool)load_sram);

    // Golf might set this to 2, so reset it here.
    tVBOpt.RENDERMODE = 1;

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
        v810_state->cycles += v810_state->cycles_until_event_full - v810_state->cycles_until_event_partial;
    }

    WORD cycles = v810_state->cycles;
    int disptime = cycles - tVIPREG.lastdisp;
    int next_event = 400000 - disptime;
    if (tVIPREG.displaying) {
        if (disptime < 60000) next_event = 60000 - disptime;
        else if (disptime < 160000) next_event = 160000 - disptime;
        else if (disptime < 200000) next_event = 200000 - disptime;
        else if (disptime < 260000) next_event = 260000 - disptime;
        else if (disptime < 360000) next_event = 360000 - disptime;
    }
    if (tHReg.TCR & 0x01) {
        int ticks = tHReg.tCount ? tHReg.tCount : tHReg.tTHW;
        if (!(tHReg.TCR & 0x10)) ticks = ticks * 5 - tHReg.ticks;
        int next_timer = ticks * 400 - (cycles - tHReg.lasttime);
        if (next_event > next_timer) next_event = next_timer;
    }
    if (tHReg.SCR & 2) {
        int next_input = tHReg.hwRead - (cycles - tHReg.lastinput);
        if (next_event > next_input) next_event = next_input;
    }
    if (tVIPREG.drawing) {
        int drawtime = cycles - tVIPREG.lastdraw;
        int sboff = (tVIPREG.rowcount) * tVIPREG.frametime / 28 + 1120;
        // the maths in serviceDisplayInt is slightly different, so add 1 to compensate
        int nextrow = (tVIPREG.rowcount + 1) * tVIPREG.frametime / 28 + 1;
        int next_draw;
        if (drawtime < sboff) next_draw = sboff - drawtime;
        else next_draw = nextrow - drawtime;
        if (next_event > next_draw) next_event = next_draw;
    }

    if (next_event < 0) next_event = 0;

    v810_state->cycles_until_event_full = v810_state->cycles_until_event_partial = next_event;
}

static int serviceDisplayInt(unsigned int cycles, WORD PC);

// Returns number of cycles until next timer interrupt.
int serviceInt(unsigned int cycles, WORD PC) {
    bool pending_int = false;

    // hardware read timing
    if (tHReg.SCR & 2) {
        int next_input = tHReg.hwRead - (cycles - tHReg.lastinput);
        tHReg.hwRead = next_input;
        if (next_input <= 0) {
            tHReg.SCR &= ~2;
            pending_int = true;
        }
    }
    tHReg.lastinput = cycles;

    // timer
    if ((cycles-tHReg.lasttime) >= 400) {
        int new_ticks = (cycles - tHReg.lasttime) / 400;
        tHReg.lasttime += 400 * new_ticks;
        int steps = (tHReg.TCR & 0x10) ? new_ticks : (tHReg.ticks + new_ticks) / 5;
        tHReg.ticks = (tHReg.ticks + new_ticks) % 5;
        if (tHReg.TCR & 0x01) { // Timer Enabled
            tHReg.tCount -= steps;
            if (tHReg.tCount <= 0 && tHReg.tCount + steps > 0) {
                tHReg.TCR |= 0x02;
                if ((tHReg.TCR & 0x09) == 0x09) tHReg.tInt = true;
            }
            while (tHReg.tCount < 0) {
                tHReg.tCount += tHReg.tTHW + 1; //reset counter
            }
            tHReg.TLB = (tHReg.tCount&0xFF);
            tHReg.THB = ((tHReg.tCount>>8)&0xFF);
        }
    }

    // graphics has higher priority, so try that first
    pending_int = serviceDisplayInt(cycles, PC) || pending_int;

    if (tHReg.tInt) {
        // zero & interrupt enabled
        pending_int = v810_int(1, PC) || pending_int;
    }

    predictEvent(false);

    return pending_int;
}

static int serviceDisplayInt(unsigned int cycles, WORD PC) {
    int gamestart;
    unsigned int disptime = (cycles - tVIPREG.lastdisp);
    bool pending_int = 0;

    if (unlikely(tVIPREG.newframe)) {
        // new frame
        tVIPREG.newframe = false;
        tVIPREG.displaying = (tVIPREG.DPCTRL & SYNCE) != 0;
        tVIPREG.DPSTTS = (tVIPREG.DPCTRL & (DISP|RE|SYNCE)) | SCANRDY | FCLK;

        int interrupts = FRAMESTART;

        if (!tVIPREG.drawing) {
            tVIPREG.lastdraw = tVIPREG.lastdisp;
        }

        if (++tVIPREG.tFrame > tVIPREG.FRMCYC) {
            tVIPREG.tFrame = 0;
            interrupts |= GAMESTART;
            if (tVIPREG.XPCTRL & XPEN) {
                if (tVIPREG.drawing) {
                    tVIPREG.XPSTTS |= OVERTIME;
                    interrupts |= TIMEERR;
                } else {
                    tVIPREG.drawing = true;
                    if (!tVBOpt.VIP_OVERCLOCK) {
                        tVIPREG.frametime = videoProcessingTime();
                    } else {
                        // pre-0.9.7 behaviour
                        tVIPREG.frametime = 137216;
                    }
                    tVIPREG.XPSTTS = XPEN | ((!tVIPREG.tDisplayedFB+1)<<2) | SBOUT;
                }
            }
        }

        tVIPREG.INTPND |= interrupts;
        pending_int = 1;
    }

    // DPSTTS management
    {
        int dpstts_old = tVIPREG.DPSTTS;
        int dpstts_new = dpstts_old;
        if (disptime >= 200000) {
            // FCLK low (high was handled already)
            dpstts_new &= ~FCLK;
        }
        if (likely(tVIPREG.displaying)) {
            if (disptime < 60000) {
            } else if (disptime < 160000) {
                // LxBSY high
                dpstts_new |= tVIPREG.tDisplayedFB & 1 ? L1BSY : L0BSY;
            } else if (disptime < 260000) {
                // LxBSY low
                dpstts_new &= ~DPBSY;
            } else if (disptime < 360000) {
                // RxBSY high
                dpstts_new |= tVIPREG.tDisplayedFB & 1 ? R1BSY : R0BSY;
            } else {
                // RxBSY low
                dpstts_new &= ~DPBSY;
            }
        }
        if (unlikely(dpstts_new != dpstts_old)) {
            tVIPREG.DPSTTS = dpstts_new;
            pending_int = 1;
            if (dpstts_old & DPBSY) {
                // old status had DPBSY, which necessarily means new one doesn't
                tVIPREG.INTPND |= (dpstts_old & (L0BSY | L1BSY)) ? LFBEND : RFBEND;
            }
        }
    }

    unsigned int drawtime = (cycles-tVIPREG.lastdraw);

    // XPSTTS management
    if (likely(tVIPREG.drawing)) {
        int rowcount = drawtime * 28 / tVIPREG.frametime;
        if (unlikely(rowcount > tVIPREG.rowcount)) {
            pending_int = 1;
            if (rowcount < 28) {
                // new row mid-frame
                tVIPREG.rowcount = rowcount;
                tVIPREG.XPSTTS = (tVIPREG.XPSTTS & 0xff) | (rowcount << 8) | SBOUT;
                // SBCMP comparison
                if (rowcount == ((tVIPREG.XPCTRL >> 8) & 0x1f)) {
                    tVIPREG.INTPND |= SBHIT;
                }
            } else {
                // finished drawing
                tVIPREG.drawing = false;
                tVIPREG.XPSTTS = 0x1b00 | (tVIPREG.XPCTRL & XPEN);
                tVIPREG.INTPND |= XPEND;
            }
        } else if (unlikely(rowcount < 28 && drawtime - rowcount * tVIPREG.frametime / 28 >= 1120)) {
            // it's been roughly 56 microseconds, so clear SBOUT
            if (tVIPREG.XPSTTS | SBOUT) pending_int = 1;
            tVIPREG.XPSTTS &= ~SBOUT;
        }
    }

    if (unlikely(disptime >= 400000)) {
        // frame end
        tVIPREG.rowcount = 0;
        v810_state->ret = 1;
        tVIPREG.lastdisp += 400000;
        tVIPREG.newframe = true;
        pending_int = 1;

        sound_update(cycles);
    }

    if (unlikely(tVIPREG.INTENB & tVIPREG.INTPND)) {
        v810_int(4, PC);
        pending_int = 1;
    }

    predictEvent(false);

    return pending_int;
}

// Generate Interupt #n
bool v810_int(WORD iNum, WORD PC) {
    if (iNum > 0x0F) return false;  // Invalid Interupt number...
    if((v810_state->S_REG[PSW] & PSW_NP)) return false;
    if((v810_state->S_REG[PSW] & PSW_EP)) return false; // Exception pending?
    if((v810_state->S_REG[PSW] & PSW_ID)) return false; // Interupt disabled
    if(iNum < ((v810_state->S_REG[PSW] & PSW_IA)>>16)) return false; // Interupt to low on the chain

    // if an interrupt happened, skip a HALT instruction if we're on one
    if (((HWORD)mem_rhword(PC) >> 10) == V810_OP_HALT) {
        PC += 2;
    }

    //Ready to Generate the Interupts
    v810_state->S_REG[EIPC]  = PC;
    v810_state->S_REG[EIPSW] = v810_state->S_REG[PSW];
    v810_state->except_flags = v810_state->flags;

    v810_state->PC = 0xFFFFFE00 | (iNum << 4);

    v810_state->S_REG[ECR] = 0xFE00 | (iNum << 4);
    v810_state->S_REG[PSW] = v810_state->S_REG[PSW] | PSW_EP;
    v810_state->S_REG[PSW] = v810_state->S_REG[PSW] | PSW_ID;
    if((iNum+=1) > 0x0F)
        (iNum = 0x0F);
    v810_state->S_REG[PSW] = v810_state->S_REG[PSW] | (iNum << 16); //Set the Interupt
    return true;
}

// Generate exception #n
// Exceptions are Div by zero, trap and Invalid Opcode, we can live without...
void v810_exp(WORD iNum, WORD eCode) {
    if (iNum > 0x0F) return;  // Invalid Exception number...

    //if(!S_REG[PSW]&PSW_ID) return;
    //if(iNum < ((S_REG[PSW] & PSW_IA)>>16)) return; // Interupt to low on the mask level....
    if ((v810_state->S_REG[PSW] & PSW_IA)>>16) return; //Interrupt Pending

    eCode &= 0xFFFF;

    if(v810_state->S_REG[PSW]&PSW_EP) { //Double Exception
        v810_state->S_REG[FEPC] = v810_state->PC;
        v810_state->S_REG[FEPSW] = v810_state->S_REG[PSW];
        v810_state->S_REG[ECR] = (eCode << 16); //Exception Code, dont get it???
        v810_state->S_REG[PSW] = v810_state->S_REG[PSW] | PSW_NP;
        v810_state->S_REG[PSW] = v810_state->S_REG[PSW] | PSW_ID;
        //S_REG[PSW] = S_REG[PSW] | (((iNum+1) & 0x0f) << 16); //Set the Interupt status

        v810_state->PC = 0xFFFFFFD0;
        return;
    } else { // Regular Exception
        v810_state->S_REG[EIPC] = v810_state->PC;
        v810_state->S_REG[EIPSW] = v810_state->S_REG[PSW];
        v810_state->except_flags = v810_state->flags;
        v810_state->S_REG[ECR] = eCode; //Exception Code, dont get it???
        v810_state->S_REG[PSW] = v810_state->S_REG[PSW] | PSW_EP;
        v810_state->S_REG[PSW] = v810_state->S_REG[PSW] | PSW_ID;
        //S_REG[PSW] = S_REG[PSW] | (((iNum+1) & 0x0f) << 16); //Set the Interupt status

        v810_state->PC = 0xFFFFFF00 | (iNum << 4);
        return;
    }
}

int v810_run(void) {
    v810_state->ret = false;

    while (true) {
        int ret = 0;
        #if DRC_AVAILABLE
        if (likely((v810_state->PC & 0x07000000) == 0x07000000)) {
            ret = drc_run();
        } else
        #endif
        {
            ret = interpreter_run();
        }
        if (ret != 0) return ret;
        if (v810_state->ret) {
            v810_state->ret = false;
            break;
        }
    }

    return 0;
}
