#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>

#include "main.h"
#include "v810_mem.h"
#include "drc_core.h"
#include "vb_dsp.h"
#include "vb_set.h"
#include "vb_sound.h"
#include "vb_gui.h"
#include "rom_db.h"

int main() {
    int qwe;
    int frame = 0;
    int err = 0;
    static int Left = 0;
    int skip = 0;
    char full_path[256] = "sdmc:/vb/";
    PrintConsole main_console;
#if DEBUGLEVEL == 0
    PrintConsole debug_console;
#endif

    gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false);
    fsInit();
    archiveMountSdmc();

#if DEBUGLEVEL == 0
    consoleInit(GFX_BOTTOM, &debug_console);
    consoleSetWindow(&debug_console, 0, 4, 40, 26);
    debug_console.flags = CONSOLE_COLOR_FAINT;
#endif
    consoleInit(GFX_BOTTOM, &main_console);

    setDefaults();
    if (loadFileOptions() < 0)
        saveFileOptions();

#if DEBUGLEVEL == 0
    if (tVBOpt.DEBUG)
        consoleDebugInit(debugDevice_CONSOLE);
    else
        consoleDebugInit(debugDevice_NULL);
#else
    consoleDebugInit(debugDevice_3DMOO);
#endif

    V810_DSP_Init();
    sound_init();

    if (tVBOpt.DSPMODE == DM_3D) {
        gfxSet3D(true);
    } else {
        gfxSet3D(false);
    }

    if (fileSelect("Load ROM", rom_name, "vb") < 0)
        goto exit;

    strncat(full_path, rom_name, 255);
    #pragma GCC diagnostic pop
    tVBOpt.ROM_NAME = rom_name;

    if (!v810_init(full_path)) {
        goto exit;
    }

    v810_reset();
    drc_init();

    clearCache();
    consoleClear();

    osSetSpeedupEnable(true);

    while(aptMainLoop()) {
        uint64_t startTime = osGetTime();

        hidScanInput();
        int keys = hidKeysDown();

        if (keys & KEY_TOUCH) {
            openMenu(&main_menu);
            if (guiop & GUIEXIT) {
                goto exit;
            }
        }

        for (qwe = 0; qwe <= tVBOpt.FRMSKIP; qwe++) {
#if DEBUGLEVEL == 0
            consoleSelect(&debug_console);
#endif
            err = drc_run();
            if (err) {
                dprintf(0, "[DRC]: error #%d @ PC=0x%08lX\n", err, v810_state->PC);
                printf("\nDumping debug info...\n");
                drc_dumpDebugInfo();
                printf("Press any key to exit\n");
                waitForInput();
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

#if DEBUGLEVEL == 0
        consoleSelect(&main_console);
        printf("\x1b[1J\x1b[0;0HFPS: %.2f\nFrame: %i\nPC: 0x%lx\nDRC cache: %.2f%%", (tVBOpt.FRMSKIP+1)*(1000./(osGetTime() - startTime)), frame, v810_state->PC, (cache_pos-cache_start)*4*100./CACHE_SIZE);
#else
        printf("\x1b[1J\x1b[0;0HFrame: %i\nPC: 0x%x", frame, (unsigned int) v810_state->PC);
#endif

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

exit:
    v810_exit();
    V810_DSP_Quit();
    sound_close();
    drc_exit();

    fsExit();
    gfxExit();
    return 0;
}
