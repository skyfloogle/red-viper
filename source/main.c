#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "vbrom_bin.h"

#include "main.h"
#include "v810_mem.h"
#include "vb_types.h"
#include "v810_cpu.h"
#include "vb_dsp.h"
#include "vb_set.h"
#include "rom_db.h"
#include "text.h"

int is_sram = 0; //Flag if writes to sram...

FS_archive sdmcArchive;

uint8_t* readFile(char* path, uint64_t* size) {
    Handle file;
    uint8_t* dest;
    uint32_t bytesRead;

    Result res = FSUSER_OpenFile(NULL, &file, sdmcArchive, FS_makePath(PATH_CHAR, path), FS_OPEN_READ, FS_ATTRIBUTE_NONE);
    if (res) {
        return 0;
    }

    FSFILE_GetSize(file, size);
    if (*size == 0) {
        FSFILE_Close(file);
        return 0;
    }

    dest = malloc(*size);
    FSFILE_Read(file, &bytesRead, 0x0, (uint32_t*)dest, *size);
    if (!bytesRead) {
        FSFILE_Close(file);
        return 0;
    }

    FSFILE_Close(file);

    return dest;
}

int v810_init(char * rom_name) {
#ifndef NOFS
    char ram_name[32];
#endif
    unsigned int rom_size = 0;
    unsigned int ram_size = 0;

#ifdef NOFS
    V810_ROM1.pmemory = vbrom_bin;
    rom_size = vbrom_bin_size;
#else
    // Open VB Rom
    V810_ROM1.pmemory = readFile(rom_name, (uint64_t*)&rom_size);
    if (!rom_size) {
        return 0;
    }
#endif

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

#ifdef NOFS
    ram_size = 0;
#else
    // Try to load up the saveRam file...
    // First, copy the rom path and concatenate .ram to it
    strcpy(ram_name, rom_name);
    strcat(ram_name, ".ram");

    V810_GAME_RAM.pmemory = readFile(ram_name, (uint64_t*)&ram_size);
#endif

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

int main() {
    int qwe;
    int frame = 0;
    int err = 0;
    static int Left = 0;
    int skip = 0;
    uint8_t* bottom_fb;

    srvInit();
    aptInit();
    hidInit(NULL);
    gfxInit();

#ifndef NOFS
    fsInit();
    sdmcArchive = (FS_archive){0x9, (FS_path){PATH_EMPTY, 1, (uint8_t*)""}};
    FSUSER_OpenArchive(NULL, &sdmcArchive);
#endif

    setDefaults();
    V810_DSP_Init();

    if (tVBOpt.DSPMODE == DM_3D) {
        gfxSet3D(true);
    } else {
        gfxSet3D(false);
    }

    if (!v810_init("rom.vb")) {
        return 1;
    }
    v810_reset();
    v810_trc();

    char info[50];
    char debug_info[50];

    clearCache();

    while(aptMainLoop()) {
        bottom_fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
        for (qwe = 0; qwe <= tVBOpt.FRMSKIP; qwe++) {
            // Trace
            // TODO: Actually check for errors and display them
            err = v810_trc();

            // Display a frame, only after the right number of 'skips'
            if((tVIPREG.FRMCYC & 0x00FF) < skip) {
                skip = 0;
                Left ^= 1;
            }

            // Increment skip
            skip++;
        }

        // Display
        if (tVIPREG.DPCTRL & 0x0002) {
            V810_Dsp_Frame(Left); //Temporary...
        }

        sprintf(info, "Frame: %i\nPC: %i", frame, (int) PC);
        sprintf(debug_info, "\n\x1b[34;1mFrame: %i\nPC: %i\x1b[0m", frame, (int) PC);
        drawString(bottom_fb, info, 0, 0);
        svcOutputDebugString(debug_info, strlen(debug_info));

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
        frame++;
    }

    V810_DSP_Quit();

#ifndef NOFS
    fsExit();
#endif

    hidExit();
    gfxExit();
    aptExit();
    srvExit();
    return 0;
}
