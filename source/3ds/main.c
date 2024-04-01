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
#include "replay.h"
#include "utils.h"

char rom_path[256] = "sdmc:/vb/";
char rom_name[128];

bool game_running = false;

Handle frame_event;
volatile int lag_frames;
void frame_pacer_thread() {
    lag_frames++;
    svcSignalEvent(frame_event);
}

int main() {
    int qwe;
    int frame = 0;
    int err = 0;
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
    ptmuInit();

    setDefaults();
    if (loadFileOptions() < 0)
        saveFileOptions();

    consoleDebugInit(debugDevice_3DMOO);

    video_init();
    guiInit();

    v810_init();

    sound_init();

    drc_init();

    if (is_citra) {
        tVBOpt.VSYNC = false;
    }

    replay_init();

    guiop = 0;
    openMenu();
    if (guiop & GUIEXIT) {
        goto exit;
    }

    game_running = true;

    clearCache();
    consoleClear();

    osSetSpeedupEnable(true);

    svcCreateEvent(&frame_event, RESET_STICKY);

    toggleVsync(tVBOpt.VSYNC);

    TickCounter drcTickCounter;
    TickCounter frameTickCounter;

    aptHookCookie cookie;
    aptHook(&cookie, aptBacklight, NULL);

    while(aptMainLoop()) {
        osTickCounterStart(&frameTickCounter);

        hidScanInput();
        int keys = hidKeysDown();

        if ((keys & KEY_TOUCH) && guiShouldPause()) {
            save_sram();
            guiop = 0;
            openMenu();
            if (guiop & GUIEXIT) {
                goto exit;
            }
            if (guiop & VBRESET) {
                v810_reset();
                drc_reset();
                clearCache();
                frame = 0;
                tVIPREG.tFrame = 0;
                tVIPREG.tFrameBuffer = 0;
            }
            if (guiop & AKILL) {
                clearCache();
                drc_clearCache();
            }
            lag_frames = 0;
        }

        // if hold, turn off fast forward, as it'll be turned back on while reading input
        if (!tVBOpt.FF_TOGGLE) tVBOpt.FASTFORWARD = false;

        // read inputs once per frame
        HWORD inputs = V810_RControll();
        tHReg.SLB =(BYTE)(inputs&0xFF);
        tHReg.SHB =(BYTE)((inputs>>8)&0xFF);

        replay_update(inputs);

        float last_drc_time = osTickCounterRead(&drcTickCounter);

#if DEBUGLEVEL == 0
        consoleSelect(&debug_console);
#endif
        osTickCounterStart(&drcTickCounter);
        err = drc_run();
        osTickCounterUpdate(&drcTickCounter);
        if (err) {
            showError(err);
            do {
                hidScanInput();
                gspWaitForVBlank();
            } while (aptMainLoop() && !hidKeysDown());
            goto exit;
        }

        // Display a frame, only after the right number of 'skips'
        if(tVIPREG.tFrame == tVIPREG.FRMCYC) {
            int alt_buf = (tVIPREG.tFrameBuffer) % 2;
            // pass C3D_FRAME_NONBLOCK to enable frameskip, 0 to disable
            // it's only needed for 1 second in the mario clash intro afaik
            // so just bite the bullet and do the frameskip, rather that than slowdown
            if (C3D_FrameBegin(C3D_FRAME_NONBLOCK)) {
                guiUpdate(osTickCounterRead(&frameTickCounter), last_drc_time);

                if (tVIPREG.DPCTRL & 0x0002) {
                    video_render(alt_buf);
                }
                C3D_FrameEnd(0);
            }
            if (tVIPREG.XPCTRL & 0x0002) {
                if (tDSPCACHE.DDSPDataState[alt_buf] != GPU_CLEAR) {
                    memset(V810_DISPLAY_RAM.pmemory + 0x8000 * alt_buf, 0, 0x6000);
                    memset(V810_DISPLAY_RAM.pmemory + 0x10000 + 0x8000 * alt_buf, 0, 0x6000);
                    tDSPCACHE.DDSPDataState[alt_buf] = CPU_CLEAR;
                }
            }
        } else {
            // no game graphics, draw menu if possible
            if (C3D_FrameBegin(C3D_FRAME_NONBLOCK)) {
                guiUpdate(osTickCounterRead(&frameTickCounter), last_drc_time);
                C3D_FrameEnd(0);
            }
        }

        // Increment frame
        frame++;

        osTickCounterUpdate(&frameTickCounter);

        if (!tVBOpt.FASTFORWARD) {
            if (lag_frames <= 0) {
                svcWaitSynchronization(frame_event, 20000000);
                lag_frames = 1;
            }
            svcClearEvent(frame_event);
            if (--lag_frames > 2)
                lag_frames = 2;
        } else {
            lag_frames = 0;
        }

#if DEBUGLEVEL == 0
        consoleSelect(&main_console);
        /*
        printf("\x1b[1J\x1b[0;0HFrame: %i\nTotal CPU: %5.2fms\tDRC: %5.2fms\nGFX-CPU: %5.2fms\tGFX-GPU: %5.2fms\nPC: 0x%lx\tDRC cache: %5.2f%%",
            frame, osTickCounterRead(&frameTickCounter), osTickCounterRead(&drcTickCounter),
            C3D_GetProcessingTime(), C3D_GetDrawingTime(),
            v810_state->PC, (cache_pos-cache_start)*4*100./CACHE_SIZE);
        */
#else
        printf("\x1b[1J\x1b[0;0HFrame: %i\nPC: 0x%x", frame, (unsigned int) v810_state->PC);
#endif
    }

    // home menu, so try and save
    save_sram();

exit:
    aptUnhook(&cookie);
    toggleVsync(false);
    sound_close();
    if (save_thread) threadJoin(save_thread, U64_MAX);
    endThreads();
    video_quit();
    drc_exit();
    v810_exit();

    ptmuExit();
    fsExit();
    gfxExit();
    return 0;
}
