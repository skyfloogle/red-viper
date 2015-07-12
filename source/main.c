#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "lsplash_bin.h"
#include "rsplash_bin.h"

#include "main.h"
#include "v810_mem.h"
#include "vb_types.h"
#include "v810_cpu.h"
#include "drc_core.h"
#include "vb_dsp.h"
#include "vb_set.h"
#include "rom_db.h"

int is_sram = 0; //Flag if writes to sram...

FS_archive sdmcArchive;

void clrScreen(int screen) {
    if ((screen != GFX_TOP) && (screen != GFX_BOTTOM))
        return;
    memset(gfxGetFramebuffer(screen, GFX_LEFT, NULL, NULL), 0,
           (GFX_BOTTOM ? 320 : 400) * 240 * 3);
}

static inline void unicodeToChar(char* dst, uint16_t* src, int max) {
    if(!src || !dst) return;
    int n = 0;
    while (*src && n < max - 1) {
        *(dst++) = (*(src++)) & 0xFF;
        n++;
    }
    *dst = 0x00;
}

void toggle3D() {
    tVBOpt.DSPMODE = !tVBOpt.DSPMODE;
    gfxSet3D(tVBOpt.DSPMODE);
}

int romSelect(char* path) {
    int pos = 1;
    int keys;
    char romv[27][100];
    int romc = 0;
    int i;

    // Scan directory. Partially taken from github.com/smealum/3ds_hb_menu
    Handle dirHandle;
    uint32_t entries_read = 1;
    FSUSER_OpenDirectory(NULL, &dirHandle, sdmcArchive, FS_makePath(PATH_CHAR, "/vb/"));
    static FS_dirent entry;

    // Scrolling isn't implemented yet
    for(i = 0; i < 29 && entries_read; i++) {
        memset(&entry, 0, sizeof(FS_dirent));
        FSDIR_Read(dirHandle, &entries_read, 1, &entry);
        if(entries_read && !entry.isDirectory) {
            //if(!strncmp("VB", (char*) entry.shortExt, 2)) {
                unicodeToChar(romv[romc], entry.name, 100);
                romc++;
            //}
        }
    }

    FSDIR_Close(dirHandle);

    while(aptMainLoop()) {
        // Draw splash screen
        // memcpy(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), lsplash_bin, lsplash_bin_size);
        // memcpy(gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, NULL, NULL), rsplash_bin, rsplash_bin_size);

        hidScanInput();
        keys = hidKeysDown();
        if(keys & KEY_DUP) {
            pos--;
        } else if (keys & KEY_DDOWN) {
            pos++;
        } else if ((keys & KEY_START) || (keys & KEY_A)) {
            break;
        } else if (keys & KEY_R) {
            // The splash screen should change to red
            tVBOpt.PALMODE = PAL_RED;
        } else if (keys & KEY_L) {
            tVBOpt.PALMODE = PAL_NORMAL;
        } else if (keys & KEY_SELECT) {
            return 0;
        } else if ((CONFIG_3D_SLIDERSTATE > 0.0f) && !tVBOpt.DSPMODE) {
            toggle3D();
        } else if ((CONFIG_3D_SLIDERSTATE == 0.0f) && tVBOpt.DSPMODE) {
            toggle3D();
        }

        if (pos > romc) {
            pos = 1;
        } else if (pos <= 0) {
            pos = romc;
        }

        printf("\x1b[;H\x1b[7mSelect a ROM:\n\x1b[0m");

        for(i = 0; i < romc; i++) {
            char line[40];
            line[0] = '\0';

            snprintf(line, 39, "%s", romv[i]);

            if ((i+1) == pos)
                printf("\x1b[1m>");
            else
                printf(" ");

            printf(line);
            printf("\x1b[0m\n");
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    strcpy(path, romv[pos-1]);
    return 1;
}

int v810_init(char * rom_name) {
    char ram_name[32];
    unsigned int rom_size = 0;
    unsigned int ram_size = 0;

    // Open VB Rom
    char full_path[46] = "sdmc:/vb/";
    strcat(full_path, rom_name);

    FILE* f = fopen(full_path, "r");
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

int main() {
    int qwe;
    int frame = 0;
    int err = 0;
    static int Left = 0;
    int skip = 0;

    gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
    fsInit();
    hbInit();
    sdmcInit();
    consoleInit(GFX_BOTTOM, NULL);
    consoleDebugInit(debugDevice_3DMOO);

    sdmcArchive = (FS_archive){0x9, (FS_path){PATH_EMPTY, 1, (uint8_t*)"/"}};
    FSUSER_OpenArchive(NULL, &sdmcArchive);

    setDefaults();
    V810_DSP_Init();

    if (tVBOpt.DSPMODE == DM_3D) {
        gfxSet3D(true);
    } else {
        gfxSet3D(false);
    }

    char path[64] = "";
    if (!romSelect(path)) {
        goto exit;
    }
    if (!v810_init(path)) {
        goto exit;
    }

    v810_reset();
    drc_init();

    clearCache();
    consoleClear();

    while(aptMainLoop()) {
        uint64_t startTime = osGetTime();

        hidScanInput();
        int keys = hidKeysHeld();

        if ((keys & KEY_X) && (keys & KEY_Y))
            break;

        for (qwe = 0; qwe <= tVBOpt.FRMSKIP; qwe++) {
            err = drc_run();
            if (err)
                break;

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

        printf("\x1b[1J\x1b[0;0HFPS: %.2f\nFrame: %i\nPC: 0x%x", (tVBOpt.FRMSKIP+1)*(1000./(osGetTime() - startTime)), frame, PC);
        //printf("Frame: %i\nPC: 0x%x", frame, (unsigned int) PC);

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

exit:
    V810_DSP_Quit();
    drc_exit();

    FSUSER_CloseArchive(NULL, &sdmcArchive);
    sdmcExit();
    fsExit();
    hbExit();
    gfxExit();
    return 0;
}
