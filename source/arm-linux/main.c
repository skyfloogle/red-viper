#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "v810_mem.h"
#include "drc_core.h"
#include "vb_dsp.h"
#include "vb_set.h"
#include "vb_gui.h"
#include "rom_db.h"

int is_sram = 0; //Flag if writes to sram...

int v810_init(char * rom_name) {
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
    if(!is_sram) {
        V810_GAME_RAM.pmemory = (unsigned char *)calloc(((V810_GAME_RAM.highaddr +1) - V810_GAME_RAM.lowaddr), sizeof(BYTE));
    }
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

    return 1;
}

int main(int argc, char* argv[]) {
    int qwe;
    int frame = 0;
    int err = 0;
    static int Left = 0;
    int skip = 0;

    V810_DSP_Init();

    if (argc < 2) {
        printf("Usage: r3Ddragon [ROM file]\n");
        return 1;
    }

    tVBOpt.ROM_NAME = argv[1];
    printf("Opening %s\n", argv[1]);

    if (!v810_init(argv[1])) {
        goto exit;
    }

    v810_reset();
    drc_init();

    clearCache();

    while(1) {
        uint64_t startTime = 0;

        int keys = 0;

//        if (0) {
//            openMenu(&main_menu);
//            if (guiop & GUIEXIT) {
//                goto exit;
//            }
//        }

        for (qwe = 0; qwe <= tVBOpt.FRMSKIP; qwe++) {
            err = drc_run();
            if (err) {
                dprintf(0, "[DRC]: error #%d @ PC=0x%08X\n", err, v810_state->PC);
                printf("\nDumping debug info...\n");
                drc_dumpDebugInfo();
                goto exit;
            }

            // Display a frame, only after the right number of 'skips'
            if((tVIPREG.FRMCYC & 0x00FF) < skip) {
                skip = 0;
                Left ^= 1;
            }

            // Increment skip
            skip++;
            frame++;
        }

        // Display
        if (tVIPREG.DPCTRL & 0x0002) {
            V810_Dsp_Frame(Left); //Temporary...
        }
    }

exit:
    V810_DSP_Quit();
    drc_exit();
    return 0;
}
