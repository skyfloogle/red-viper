//////////////////////////////////////////////////////////
// Main CPU routines

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "minizip/unzip.h"

#include "v810_ins.h"
#include "v810_opt.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_types.h"
#include "vb_set.h"
#include "rom_db.h"
#include "drc_core.h"
#include "interpreter.h"
#include "vb_sound.h"

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
    0x03,0x03,0x0D,0x05,0x01,0x01,0x00,0x01,0x03,0x03,0x1A,0x05,0x01,0x01,0x00,0x01, //these are based on 16-bit bus!! (should be 32-bit?)
    0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x01,0x03,0x03
};

void v810_init() {
    char ram_name[32];
    unsigned int ram_size = 0;

    V810_ROM1.pmemory = malloc(MAX_ROM_SIZE);

    // Initialize our ram1 tables....
    V810_DISPLAY_RAM.lowaddr  = 0x00000000;
    V810_DISPLAY_RAM.highaddr = 0x0003FFFF; //0x0005FFFF; //97FFF
    // Alocate space for it in memory
    V810_DISPLAY_RAM.pmemory = (unsigned char *)calloc(((V810_DISPLAY_RAM.highaddr +1) - V810_DISPLAY_RAM.lowaddr), sizeof(BYTE));
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
    // Alocate space for it in memory
    V810_SOUND_RAM.pmemory = (unsigned char *)malloc(((V810_SOUND_RAM.highaddr +1) - V810_SOUND_RAM.lowaddr) * sizeof(BYTE));
    // Offset + Lowaddr = pmemory
    V810_SOUND_RAM.off = (size_t)V810_SOUND_RAM.pmemory - V810_SOUND_RAM.lowaddr;

    // Initialize our VBRam tables....
    V810_VB_RAM.lowaddr  = 0x05000000;
    V810_VB_RAM.highaddr = 0x0500FFFF;
    // Alocate space for it in memory
    V810_VB_RAM.pmemory = (unsigned char *)malloc(((V810_VB_RAM.highaddr +1) - V810_VB_RAM.lowaddr) * sizeof(BYTE));
    // Offset + Lowaddr = pmemory
    V810_VB_RAM.off = (size_t)V810_VB_RAM.pmemory - V810_VB_RAM.lowaddr;

    // Initialize our GameRam tables.... (Cartrige Ram)
    V810_GAME_RAM.lowaddr  = 0x06000000;
    V810_GAME_RAM.highaddr = 0x06003FFF; //0x06007FFF; //(8K, not 64k!)
    // Alocate space for it in memory
    V810_GAME_RAM.pmemory = (unsigned char *)calloc(((V810_GAME_RAM.highaddr +1) - V810_GAME_RAM.lowaddr), sizeof(BYTE));
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
int v810_load_init() {
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
                bool rom_size_valid = info.uncompressed_size <= MAX_ROM_SIZE;
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
        if (!load_file) return UNZ_ERRNO;
        struct stat mystat;
        fstat(fileno(load_file), &mystat);
        rom_size = mystat.st_size;
        bool rom_size_valid = rom_size <= MAX_ROM_SIZE;
        // require po2
        rom_size_valid = rom_size_valid && !(rom_size & (rom_size - 1));
        if (!rom_size_valid) {
            fclose(load_file);
            return UNZ_ERRNO;
        }
    }
    ok:
    // Initialize our rom tables.... (USA)
    V810_ROM1.highaddr = 0x07000000 + rom_size - 1;
    V810_ROM1.lowaddr  = 0x07000000;
    V810_ROM1.off = (size_t)V810_ROM1.pmemory - V810_ROM1.lowaddr;
    // Offset + Lowaddr = pmemory

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

int v810_load_step() {
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
                return UNZ_ERRNO;
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
            return UNZ_ERRNO;
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

        v810_reset();

        return 100;
    }
    return load_pos * 100 / all_size;
}

void v810_load_cancel() {
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

void v810_exit() {
    free(v810_state);
    free(V810_ROM1.pmemory);
    free(V810_DISPLAY_RAM.pmemory);
    free(V810_SOUND_RAM.pmemory);
    free(V810_VB_RAM.pmemory);
    free(V810_GAME_RAM.pmemory);
}

// Reinitialize the defaults in the CPU
void v810_reset() {
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

    tHReg.tTRC = 2000;
    tHReg.tCount = 0xFFFF;
    tHReg.tReset = 0;

    tHReg.hwRead = 0;

    // we don't reset load_sram so it will be non-null if there was sram to load
    replay_reset(is_sram || (bool)load_sram);
}

// Returns number of cycles until next timer interrupt.
int serviceInt(unsigned int cycles, WORD PC) {
    const static int MAXCYCLES = 512;

    //OK, this is a strange muck of code... basically it attempts to hit interrupts and
    //handle the VIP regs at the correct time. The timing needs a LOT of work. Right now,
    //the count values I'm using are the best values from my old clock cycle table. In
    //other words, the values are so far off. PBBT!  FIXME

    //For whatever reason we dont need this code
    //actualy it totaly breaks the emu if you don't call it on
    //every cycle, fixme, what causes this to error out.
    //Controller Int
    //if ((!(tHReg.SCR & 0x80)) && (handle_input()&0xFFFC)) {
    //  v810_int(0);
    //}

    bool fast_timer = !!(tHReg.TCR & 0x10);
    int next_timer = (tHReg.TCR & 0x01) && tHReg.tCount != 0 ? tHReg.tCount * (fast_timer ? 400 : 2000) - (cycles - tHReg.lasttime) : MAXCYCLES;
    int next_input = tHReg.SCR & 2 ? tHReg.hwRead - (cycles - tHReg.lastinput) : MAXCYCLES;
    int next_interrupt = next_timer < next_input ? next_timer : next_input;
    next_interrupt = next_interrupt < MAXCYCLES ? next_interrupt : MAXCYCLES;

    // hardware read timing
    if (tHReg.SCR & 2) {
        tHReg.hwRead = next_input;
        if (next_input <= 0)
            tHReg.SCR &= ~2;
    }
    tHReg.lastinput = cycles;

    if ((cycles-tHReg.lasttime) >= 400) {
        int new_ticks = (cycles - tHReg.lasttime) / 400;
        tHReg.lasttime += 400 * new_ticks;
        int steps = fast_timer ? new_ticks : (tHReg.ticks + new_ticks) / 5;
        tHReg.ticks = (tHReg.ticks + new_ticks) % 5;
        if (tHReg.TCR & 0x01) { // Timer Enabled
            tHReg.tCount -= steps;
            // Sometimes (Nester's Funky Bowling) there's more steps than the
            // timer has ticks. This shouldn't happen, but in the meantime
            // make sure not to crash it.
            while (tHReg.tCount <= 0) {
                tHReg.tCount += tHReg.tTHW + 1; //reset counter
                tHReg.TCR |= 0x02; //Zero Status
            }
            tHReg.TLB = (tHReg.tCount&0xFF);
            tHReg.THB = ((tHReg.tCount>>8)&0xFF);
        }
    }
    if ((tHReg.TCR & 0x01) && (tHReg.TCR & 0x02) && (tHReg.TCR & 0x08)) {
        // zero & interrupt enabled
        return v810_int(1, PC) ? 0 : next_input;
    }
    return next_interrupt;
}

int serviceDisplayInt(unsigned int cycles, WORD PC) {
    int gamestart;
    unsigned int tfb = (cycles-tVIPREG.lastfb);
    bool pending_int = 0;
    
    v810_state->PC = PC;

    //Handle DPSTTS, XPSTTS, and Frame interrupts
    if (tVIPREG.rowcount < 0x1C) {
        if (tVIPREG.newframe) {
            // new frame
            tVIPREG.newframe = false;
            gamestart = 0;
            if (tVIPREG.DPCTRL & 0x02) {
                tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|0xC0);
            }
            if (++tVIPREG.tFrame > tVIPREG.FRMCYC) {
                tVIPREG.tFrame = 0;
                gamestart = 8;
                if (tVIPREG.XPCTRL & 0x02) {
                    tVIPREG.drawing = true;
                    tVIPREG.tFrameBuffer++;
                    if ((tVIPREG.tFrameBuffer < 1) || (tVIPREG.tFrameBuffer > 2)) tVIPREG.tFrameBuffer = 1;
                    tVIPREG.XPSTTS = (0x0002|(tVIPREG.tFrameBuffer<<2));
                }
            }
            if (tVIPREG.INTENB&(0x0010|gamestart)) {
                v810_int(4, PC);
            }
            tVIPREG.INTPND |= (0x0010|gamestart);
            pending_int = 1;
        } else if ((tfb > 0x0500) && (!(tVIPREG.XPSTTS&0x8000))) {
            tVIPREG.XPSTTS |= 0x8000;
            pending_int = 1;
        } else if (tfb > 0x0A00) {
            if (tVIPREG.drawing) tVIPREG.XPSTTS = ((tVIPREG.XPSTTS&0xEC)|(tVIPREG.rowcount<<8)|(tVIPREG.XPCTRL & 0x02));
            tVIPREG.rowcount++;
            tVIPREG.lastfb+=0x0A00;
        } else if ((tVIPREG.rowcount == 0x12) && (tfb > 0x670)) {
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL & 0x0302) | (tVIPREG.tFrameBuffer & 1 ? 0xD0 : 0xC4));
            pending_int = 1;
        }
    } else {
        if ((tVIPREG.rowcount == 0x1C) && (tfb > 0x10000)) {            //0x100000
            if (tVIPREG.drawing) {
                tVIPREG.drawing = false;

                tVIPREG.XPSTTS = (0x1B00 | (tVIPREG.XPCTRL & 0x02));
                pending_int = 1;

                if (tVIPREG.INTENB & 0x4000) {
                    v810_int(4, PC);                    //XPEND
                }

                tVIPREG.INTPND |= 0x4000;               //(tVIPREG.INTENB&0x4000);
            }
            tVIPREG.rowcount++;
        } else if ((tVIPREG.rowcount == 0x1D) && (tfb > 0x18000)) {     //0xE690
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|0xC0);
            if (tVIPREG.INTENB&0x0002) {
                v810_int(4, PC);                    //LFBEND
                pending_int = 1;
            }
            tVIPREG.INTPND |= 0x0002;               //(tVIPREG.INTENB&0x0002);
            pending_int = 1;
            tVIPREG.rowcount++;
        } else if ((tVIPREG.rowcount == 0x1E) && (tfb > 0x20000)) {     //0x15E70
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|0x40);
            if (tVIPREG.INTENB&0x0004) {
                v810_int(4, PC);                    //RFBEND
            }
            tVIPREG.INTPND |= 0x0004;               //(tVIPREG.INTENB&0x0004);
            pending_int = 1;
            tVIPREG.rowcount++;
        } else if ((tVIPREG.rowcount == 0x1F) && (tfb > 0x28000)) {     //0x1FAD8
            //tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|((tVIPREG.tFrameBuffer&1)?0x48:0x60));
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|((tVIPREG.tFrameBuffer&1)?0x60:0x48)); //if editing FB0, shouldn't be drawing FB0
            if (tVIPREG.INTENB&0x2000) {
                v810_int(4, PC);                    //SBHIT
            }
            tVIPREG.INTPND |= 0x2000;
            pending_int = 1;
            tVIPREG.rowcount++;
        } else if ((tVIPREG.rowcount == 0x20) && (tfb > 0x38000)) {     //0x33FD8
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|0x40);
            tVIPREG.rowcount++;
        } else if ((tVIPREG.rowcount == 0x21) && (tfb > 0x50280)) {
            // frame end
            tVIPREG.rowcount=0;
            v810_state->ret = 1;
            tVIPREG.lastfb+=0x50280;
            tVIPREG.newframe = true;
            pending_int = 1;

            sound_update(cycles);
        }
    }

    if (!pending_int) {
        if (tVIPREG.INTENB & tVIPREG.INTPND) {
            v810_int(4, PC);
            pending_int = 1;
        }
    }

    return pending_int;
}

// Generate Interupt #n
bool v810_int(WORD iNum, WORD PC) {
    if (iNum > 0x0F) return false;  // Invalid Interupt number...
    if((v810_state->S_REG[PSW] & PSW_NP)) return false;
    if((v810_state->S_REG[PSW] & PSW_EP)) return false; // Exception pending?
    if((v810_state->S_REG[PSW] & PSW_ID)) return false; // Interupt disabled
    if(iNum < ((v810_state->S_REG[PSW] & PSW_IA)>>16)) return false; // Interupt to low on the chain

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

int v810_run() {
    v810_state->ret = false;

    while (true) {
        int ret = 0;
        #if DRC_AVAILABLE
        if ((v810_state->PC & 0x07000000) == 0x07000000) {
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
