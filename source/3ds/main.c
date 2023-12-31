#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <3ds.h>
#include <citro3d.h>

#include "main.h"
#include "periodic.h"
#include "v810_mem.h"
#include "drc_core.h"
#include "vb_dsp.h"
#include "vb_set.h"
#include "vb_sound.h"
#include "vb_gui.h"
#include "rom_db.h"

char rom_path[256] = "sdmc:/vb/";
char rom_name[128];

int main() {
    int qwe;
    int frame = 0;
    int err = 0;
    int alt_buf = 1; // 1 is required for golf
    int skip = 0;
    bool loaded = false;
    PrintConsole main_console;
#if DEBUGLEVEL == 0
    PrintConsole debug_console;
#endif
    Handle nothingEvent = 0;
    svcCreateEvent(&nothingEvent, 0);

    // gfxInit(GSP_RGB565_OES, GSP_RGB565_OES, false); // legacy renderer
    gfxInitDefault(); // hardware renderer
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
    video_init();

    if (tVBOpt.DSPMODE == DM_3D) {
        gfxSet3D(true);
    } else {
        gfxSet3D(false);
    }

    if (fileSelect("Load ROM", rom_name, "vb") < 0)
        goto exit;

    strncat(rom_path, rom_name, 255);
    #pragma GCC diagnostic pop
    tVBOpt.ROM_NAME = rom_name;

    if (!v810_init(rom_path)) {
        goto exit;
    }
    loaded = true;

    if (tVBOpt.SOUND) sound_init();

    v810_reset();
    drc_init();

    clearCache();
    consoleClear();

    osSetSpeedupEnable(true);

    Handle frameTimer;
    svcCreateTimer(&frameTimer, RESET_STICKY);
    svcSetTimer(frameTimer, 0, 20000000);

    TickCounter drcTickCounter;
    TickCounter frameTickCounter;

    while(aptMainLoop()) {
        osTickCounterStart(&frameTickCounter);

        hidScanInput();
        int keys = hidKeysDown();

        if (keys & KEY_TOUCH) {
            guiop = 0;
            openMenu(&main_menu);
            if (guiop & GUIEXIT) {
                goto exit;
            }
            if (guiop & VBRESET) {
                if (tVBOpt.SOUND) sound_close();
                int oldSound = tVBOpt.SOUND;
                tVBOpt.SOUND = false;
                v810_exit();
                if (!v810_init(rom_path))
                    goto exit;
                v810_reset();
                drc_reset();
                clearCache();
                tVBOpt.SOUND = oldSound;
                if (tVBOpt.SOUND) sound_init();
                frame = 0;
                skip = 0;
                alt_buf = 1;
            }
            if (guiop & AKILL) {
                clearCache();
                drc_clearCache();
            }
        }

#if DEBUGLEVEL == 0
        consoleSelect(&debug_console);
#endif
        osTickCounterStart(&drcTickCounter);
        err = drc_run();
        osTickCounterUpdate(&drcTickCounter);
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
            if (tVIPREG.DPCTRL & 0x0002) {
                video_render(alt_buf);
            }
            if (tVIPREG.XPCTRL & 0x0002) {
                memset(V810_DISPLAY_RAM.pmemory + 0x8000 * alt_buf, 0, 0x6000);
                memset(V810_DISPLAY_RAM.pmemory + 0x10000 + 0x8000 * alt_buf, 0, 0x6000);
                alt_buf = !alt_buf;
            }
        }

        // Increment skip
        skip++;
        frame++;

        osTickCounterUpdate(&frameTickCounter);

        if (!tVBOpt.FASTFORWARD) {
            svcWaitSynchronization(frameTimer, 20000000);
            svcClearTimer(frameTimer);
        }

#if DEBUGLEVEL == 0
        consoleSelect(&main_console);
        printf("\x1b[1J\x1b[0;0HFrame: %i\nTotal CPU: %5.2fms\tDRC: %5.2fms\nGFX-CPU: %5.2fms\tGFX-GPU: %5.2fms\nPC: 0x%lx\tDRC cache: %5.2f%%",
            frame, osTickCounterRead(&frameTickCounter), osTickCounterRead(&drcTickCounter),
            C3D_GetProcessingTime(), C3D_GetDrawingTime(),
            v810_state->PC, (cache_pos-cache_start)*4*100./CACHE_SIZE);
#else
        printf("\x1b[1J\x1b[0;0HFrame: %i\nPC: 0x%x", frame, (unsigned int) v810_state->PC);
#endif
    }

exit:
    v810_exit();
    endThreads();
    V810_DSP_Quit();
    video_quit();
    if (tVBOpt.SOUND && loaded) sound_close();
    drc_exit();

    fsExit();
    gfxExit();
    return 0;
}
