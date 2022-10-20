//////////////////////////////////////////////////////////
// Main CPU routines

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "v810_ins.h"
#include "v810_opt.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_types.h"
#include "vb_set.h"
#include "rom_db.h"
#include "drc_core.h"

#define NEG(n) ((n) >> 31)
#define POS(n) ((~(n)) >> 31)

////////////////////////////////////////////////////////////
// Globals

const BYTE opcycle[0x50] = {
    0x01,0x01,0x01,0x01,0x01,0x01,0x03,0x01,0x0D,0x26,0x0D,0x24,0x01,0x01,0x01,0x01,
    0x01,0x01,0x01,0x01,0x01,0x01,0x03,0x01,0x0F,0x0A,0x05,0x00,0x01,0x01,0x03,0x00, //EI, HALT, LDSR, STSR, DI, BSTR -- Unknown clocks
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x01,0x03,0x03,0x01,0x01,0x01,0x01,
    0x01,0x01,0x0D,0x01,0x01,0x01,0x00,0x01,0x03,0x03,0x1A,0x05,0x01,0x01,0x00,0x01, //these are based on 16-bit bus!! (should be 32-bit?)
    0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01
};

int v810_init(char *rom_name) {
    char ram_name[32];
    unsigned int rom_size = 0;
    unsigned int ram_size = 0;

    // Open VB Rom
    FILE* f = fopen(rom_name, "r");
    if (f) {
        fseek(f , 0 , SEEK_END);
        rom_size = ftell(f);
        rewind(f);

        V810_ROM1.pmemory = malloc(rom_size);
        fread(V810_ROM1.pmemory, 1, rom_size, f);

        fclose(f);
    } else {
        return 0;
    }

    // CRC32 Calculations
    gen_table();
    tVBOpt.CRC32 = get_crc(rom_size);

    // Initialize our rom tables.... (USA)
    V810_ROM1.highaddr = 0x07000000 + rom_size - 1;
    V810_ROM1.lowaddr  = 0x07000000;
    V810_ROM1.off = (unsigned)V810_ROM1.pmemory - V810_ROM1.lowaddr;
    // Offset + Lowaddr = pmemory

    // Initialize our ram1 tables....
    V810_DISPLAY_RAM.lowaddr  = 0x00000000;
    V810_DISPLAY_RAM.highaddr = 0x0003FFFF; //0x0005FFFF; //97FFF
    // Alocate space for it in memory
    V810_DISPLAY_RAM.pmemory = (unsigned char *)malloc(((V810_DISPLAY_RAM.highaddr +1) - V810_DISPLAY_RAM.lowaddr) * sizeof(BYTE));
    // Offset + Lowaddr = pmemory
    V810_DISPLAY_RAM.off = (unsigned)V810_DISPLAY_RAM.pmemory - V810_DISPLAY_RAM.lowaddr;

    // Initialize our VIPC Reg tables....
    V810_VIPCREG.lowaddr  = 0x00040000; //0x0005F800
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
    V810_SOUND_RAM.highaddr = 0x010005FF; //0x010002FF
    // Alocate space for it in memory
    V810_SOUND_RAM.pmemory = (unsigned char *)malloc(((V810_SOUND_RAM.highaddr +1) - V810_SOUND_RAM.lowaddr) * sizeof(BYTE));
    // Offset + Lowaddr = pmemory
    V810_SOUND_RAM.off = (unsigned)V810_SOUND_RAM.pmemory - V810_SOUND_RAM.lowaddr;

    // Initialize our VBRam tables....
    V810_VB_RAM.lowaddr  = 0x05000000;
    V810_VB_RAM.highaddr = 0x0500FFFF;
    // Alocate space for it in memory
    V810_VB_RAM.pmemory = (unsigned char *)malloc(((V810_VB_RAM.highaddr +1) - V810_VB_RAM.lowaddr) * sizeof(BYTE));
    // Offset + Lowaddr = pmemory
    V810_VB_RAM.off = (unsigned)V810_VB_RAM.pmemory - V810_VB_RAM.lowaddr;

    // Try to load up the saveRam file...
    // First, copy the rom path and concatenate .ram to it
    // strcpy(ram_name, rom_name);
    // strcat(ram_name, ".ram");

    // V810_GAME_RAM.pmemory = readFile(ram_name, (uint64_t*)&ram_size);

    if (!ram_size) {
        is_sram = 0;
    } else {
        is_sram = 1;
    }

    // Initialize our GameRam tables.... (Cartrige Ram)
    V810_GAME_RAM.lowaddr  = 0x06000000;
    V810_GAME_RAM.highaddr = 0x06003FFF; //0x06007FFF; //(8K, not 64k!)
    // Alocate space for it in memory
    V810_GAME_RAM.pmemory = (unsigned char *)calloc(((V810_GAME_RAM.highaddr +1) - V810_GAME_RAM.lowaddr), sizeof(BYTE));
    // Offset + Lowaddr = pmemory
    V810_GAME_RAM.off = (unsigned)V810_GAME_RAM.pmemory - V810_GAME_RAM.lowaddr;

    if(ram_size > (V810_GAME_RAM.highaddr+1) - V810_GAME_RAM.lowaddr) {
        ram_size = (V810_GAME_RAM.highaddr +1) - V810_GAME_RAM.lowaddr;
    }

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

    v810_state = calloc(1, sizeof(cpu_state));

    return 1;
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

    v810_state->irq_handler = &drc_handleInterrupts;
    v810_state->reloc_table = &drc_relocTable;

    v810_state->P_REG[0]    =  0x00000000;
    v810_state->PC          =  0xFFFFFFF0;
    v810_state->S_REG[ECR]  =  0x0000FFF0;
    v810_state->S_REG[PSW]  =  0x00008000;
    v810_state->S_REG[PIR]  =  0x00005346;
    v810_state->S_REG[TKCW] =  0x000000E0;
}

int serviceInt(unsigned int cycles, WORD PC) {
    static unsigned int lasttime=0;

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

    if (tHReg.TCR & 0x01) { // Timer Enabled
        if ((cycles-lasttime) > tHReg.tTRC) {
            if (tHReg.tCount)
                tHReg.tCount--;
            tHReg.TLB = (tHReg.tCount&0xFF);
            tHReg.THB = ((tHReg.tCount>>8)&0xFF);
            lasttime=cycles;
            if (tHReg.tCount == 0) {
                tHReg.tCount = tHReg.tTHW; //reset counter
                tHReg.TCR |= 0x02; //Zero Status
                if (tHReg.TCR & 0x08) {
                    v810_int(1, PC);
                    return 1;
                }
            }
        }
    }

    return 0;
}

int serviceDisplayInt(unsigned int cycles, WORD PC) {
    static unsigned int lastfb=0;
    static int rowcount,tmp1,frames=0;
    int gamestart;
    unsigned int tfb = (cycles-lastfb);
    bool pending_int = 0;

    //Handle DPSTTS, XPSTTS, and Frame interrupts
    if (rowcount < 0x1C) {
        if ((rowcount == 0) && (tfb > 0x0210) && (!tmp1)) {
            tmp1=1;
            tVIPREG.XPSTTS &= 0x000F;
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|0xC0);
            if (++frames > tVIPREG.FRMCYC) {
                frames = 0;
                gamestart = 0x0008;
            } else {
                gamestart = 0;
            }
            if (tVIPREG.INTENB&(0x0010|gamestart)) {
                v810_int(4, PC);
                pending_int = 1;
            }
            tVIPREG.INTPND |= (0x0010|gamestart);

            v810_state->ret = 1;
            return 1;
        } else if ((tfb > 0x0500) && (!(tVIPREG.XPSTTS&0x8000))) {
            tVIPREG.XPSTTS |= 0x8000;
        } else if (tfb > 0x0A00) {
            tVIPREG.XPSTTS = ((tVIPREG.XPSTTS&0xE0)|(rowcount<<8)|(tVIPREG.XPCTRL & 0x02));
            rowcount++;
            lastfb=cycles;
        } else if ((rowcount == 0x12) && (tfb > 0x670)) {
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL & 0x0302) | (tVIPREG.tFrame & 1 ? 0xD0 : 0xC4));
        }
    } else {
        if ((rowcount == 0x1C) && (tfb > 0x10000)) {            //0x100000
            tVIPREG.XPSTTS = (0x1B00 | (tVIPREG.XPCTRL & 0x02));

            /*if(tVBOpt.VFHACK)                   //vertical force hack
                v810_int(4);
            else */if (tVIPREG.INTENB & 0x4000) {
                v810_int(4, PC);                    //XPEND
                pending_int = 1;
            }

            tVIPREG.INTPND |= 0x4000;               //(tVIPREG.INTENB&0x4000);
            rowcount++;
        } else if ((rowcount == 0x1D) && (tfb > 0x18000)) {     //0xE690
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|0xC0);
            if (tVIPREG.INTENB&0x0002) {
                v810_int(4, PC);                    //LFBEND
                pending_int = 1;
            }
            tVIPREG.INTPND |= 0x0002;               //(tVIPREG.INTENB&0x0002);
            rowcount++;
        } else if ((rowcount == 0x1E) && (tfb > 0x20000)) {     //0x15E70
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|0x40);
            if (tVIPREG.INTENB&0x0004) {
                v810_int(4, PC);                    //RFBEND
                pending_int = 1;
            }
            tVIPREG.INTPND |= 0x0004;               //(tVIPREG.INTENB&0x0004);
            rowcount++;
        } else if ((rowcount == 0x1F) && (tfb > 0x28000)) {     //0x1FAD8
            //tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|((tVIPREG.tFrame&1)?0x48:0x60));
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|((tVIPREG.tFrame&1)?0x60:0x48)); //if editing FB0, shouldn't be drawing FB0
            if (tVIPREG.INTENB&0x2000) {
                v810_int(4, PC);                    //SBHIT
                pending_int = 1;
            }
            tVIPREG.INTPND |= 0x2000;
            rowcount++;
        } else if ((rowcount == 0x20) && (tfb > 0x38000)) {     //0x33FD8
            tVIPREG.DPSTTS = ((tVIPREG.DPCTRL&0x0302)|0x40);
            rowcount++;
        } else if ((rowcount == 0x21) && (tfb > 0x42000)) {
            tmp1=0;
            rowcount=0;
            tVIPREG.tFrame++;
            if ((tVIPREG.tFrame < 1) || (tVIPREG.tFrame > 2)) tVIPREG.tFrame = 1;
            tVIPREG.XPSTTS = (0x1B00|(tVIPREG.tFrame<<2)|(tVIPREG.XPCTRL & 0x02));
            //if (tVIPREG.XPSTTS&2) //clear screen buffer
            //{
            memset((BYTE *)(V810_DISPLAY_RAM.off+(tVIPREG.tFrame-1)*0x8000),0,0x6000);
            memset((BYTE *)(V810_DISPLAY_RAM.off+((tVIPREG.tFrame-1)+2)*0x8000),0,0x6000);
            //}
            lastfb=cycles;
        }
    }

    if (!pending_int)
        v810_state->PC = PC;

    return pending_int;
}

// Generate Interupt #n
void v810_int(WORD iNum, WORD PC) {
    if (iNum > 0x0F) return;  // Invalid Interupt number...
    if((v810_state->S_REG[PSW] & PSW_NP)) return;
    if((v810_state->S_REG[PSW] & PSW_EP)) return; // Exception pending?
    if((v810_state->S_REG[PSW] & PSW_ID)) return; // Interupt disabled
    if(iNum < ((v810_state->S_REG[PSW] & PSW_IA)>>16)) return; // Interupt to low on the chain

    dprintf(1, "[INT]: iNum=0x%lx\n", iNum);

    //Ready to Generate the Interupts
    v810_state->S_REG[EIPC]  = PC;
    v810_state->S_REG[EIPSW] = v810_state->S_REG[PSW];

    v810_state->PC = 0xFFFFFE00 | (iNum << 4);

    v810_state->S_REG[ECR] = 0xFE00 | (iNum << 4);
    v810_state->S_REG[PSW] = v810_state->S_REG[PSW] | PSW_EP;
    v810_state->S_REG[PSW] = v810_state->S_REG[PSW] | PSW_ID;
    if((iNum+=1) > 0x0F)
        (iNum = 0x0F);
    v810_state->S_REG[PSW] = v810_state->S_REG[PSW] | (iNum << 16); //Set the Interupt
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
        v810_state->S_REG[ECR] = eCode; //Exception Code, dont get it???
        v810_state->S_REG[PSW] = v810_state->S_REG[PSW] | PSW_EP;
        v810_state->S_REG[PSW] = v810_state->S_REG[PSW] | PSW_ID;
        //S_REG[PSW] = S_REG[PSW] | (((iNum+1) & 0x0f) << 16); //Set the Interupt status

        v810_state->PC = 0xFFFFFF00 | (iNum << 4);
        return;
    }
}
