#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include "main.h"
#include "v810_mem.h"
#include "drc_core.h"
#include "vb_dsp.h"
#include "vb_set.h"
#include "vb_gui.h"
#include "rom_db.h"

int arm_keys;
void sigint_handler(int sig) {
    int i;
    if (!scanf("%d", &i)) {
        drc_dumpDebugInfo();
        v810_exit();
        V810_DSP_Quit();
        drc_exit();
        exit(1);
    }
    arm_keys = 0xffffffff;
}

int main(int argc, char* argv[]) {
    int qwe;
    int frame = 0;
    int err = 0;
    static int Left = 0;
    int skip = 0;
    signal(SIGINT, sigint_handler);

    setDefaults();
    if (loadFileOptions() < 0)
        saveFileOptions();

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
    v810_exit();
    V810_DSP_Quit();
    drc_exit();
    return 0;
}
