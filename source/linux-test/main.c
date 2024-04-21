#include <stdio.h>
#include <unistd.h>
#include "vb_set.h"
#include "v810_cpu.h"
#include "replay.h"

// dummy
#include "vb_dsp.h"
#include "vb_sound.h"
VB_DSPCACHE tDSPCACHE;
void sound_update(uint32_t cycles) {}
void sound_write(int addr, uint16_t val) {}
int drc_handleInterrupts(WORD cpsr, WORD* PC) { return 0; }
void drc_relocTable(void) {}

int main(int argc, char* argv[]) {
    int err;

    if (argc != 2) {
        puts("Pass a ROM please");
        return 1;
    }

    if (access(argv[1], F_OK) != 0) {
        printf("Error: ROM %s doesn't exist\n", argv[1]);
        return 1;
    }

    setDefaults();
    v810_init();
    replay_init();

    tVBOpt.ROM_PATH = argv[1];

    v810_load_init();
    while (true) {
        int ret = v810_load_step();
        if (ret < 0) return ret;
        if (ret == 100) break;
    }

    while (true) {
        err = v810_run();
        if (err) {
            printf("Error code %d\n", err);
            return 1;
        }
    }

    return 0;
}