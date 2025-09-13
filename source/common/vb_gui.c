#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>

#ifdef __3DS__
#include <3ds.h>
#endif

#include "vb_types.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_dsp.h"
#include "vb_gui.h"
#include "vb_set.h"
#include "vb_sound.h"

#ifdef __3DS__
#define GUI_STUB(void) { \
    consoleClear(); \
    printf("STUBBED"); \
    waitForInput(); \
}
#else
#define GUI_STUB()
#define consoleClear()
#endif

struct stat st = {0};
int guiop;

u32 waitForInput(void) {
#ifdef __3DS__
    u32 keys;
    do {
        hidScanInput();
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    } while (!(keys = hidKeysDown()));

    return keys;
#else
    return getc(stdin);
#endif
}

#ifdef __3DS__
Thread save_thread = NULL;
#else
void *save_thread = NULL;
#endif

static void save_sram_thread(void *sram) {
    FILE *nvramf = fopen(tVBOpt.RAM_PATH, "wb");

    // Save out the sram ram
    if (nvramf == NULL) {
        printf("\nError opening sram.bin");
        return;
    }
    fwrite(sram, 1, vb_state->V810_GAME_RAM.highaddr + 1 - vb_state->V810_GAME_RAM.lowaddr, nvramf);
    fclose(nvramf);

    free(sram);

    save_thread = NULL;
}

void save_sram(void) {
    // Do not save sram if it's not required!
    if (!is_sram) return;
    // Don't save if we're already saving
    if (save_thread) return;
    void *sram_copy = malloc(vb_state->V810_GAME_RAM.highaddr + 1 - vb_state->V810_GAME_RAM.lowaddr);
    memcpy(sram_copy, vb_state->V810_GAME_RAM.pmemory, vb_state->V810_GAME_RAM.highaddr + 1 - vb_state->V810_GAME_RAM.lowaddr);
#ifdef __3DS__
    // Saving on the same thread is slow, but saving in another thread happens instantly
    save_thread = threadCreate(save_sram_thread, sram_copy, 4000, 0x18, 1, true);
    if (!save_thread) {
        puts("oh no");
    }
#else
    save_sram_thread(sram_copy);
#endif
}

char * get_savestate_path(int state, bool write) {
    char *last_slash = strrchr(tVBOpt.ROM_PATH, '/');
    if (last_slash == NULL) return NULL;
    // maxpath measured to be around 260, but pick 300 just to be safe
    #define MAX_PATH_LEN 300
    // $HOME/savestates
    char sshome[MAX_PATH_LEN - 20];
    snprintf(sshome, sizeof(sshome), "%s/savestates", tVBOpt.HOME_PATH);
    // $HOME/savestates/game
    char *sspath = (char *) malloc(MAX_PATH_LEN * sizeof(char));
    snprintf(sspath, MAX_PATH_LEN, "%s/%s", sshome, last_slash + 1);
    int sspath_len = strlen(sspath);
    char *end = strrchr(sspath, '.');
    if (!end) end = sspath + strlen(sspath);
    if (end - sspath + 20 >= MAX_PATH_LEN) goto bail;
    *end = 0;
    if (stat(tVBOpt.HOME_PATH, &st) == -1) {
        if (!write || mkdir(tVBOpt.HOME_PATH, 0777)) goto bail;
    }
    if (stat(sshome, &st) == -1) {
        if (!write || mkdir(sshome, 0777)) goto bail;
    }
    if (stat(sspath, &st) == -1) {
        if (!write || mkdir(sspath, 0777)) goto bail;
    }
    snprintf(end, 10, "/st%d.rvs", state);
    return sspath;

    bail:
    free(sspath);
    return NULL;
}

#ifdef __3DS__

bool emulation_hasstate(int state) {
    char *sspath = get_savestate_path(state, false);
    if (sspath == NULL) return false;
    bool result = stat(sspath, &st) != -1;
    free(sspath);
    return result;
}

int emulation_rmstate(int state) {
    char* sspath = get_savestate_path(state, false);
    if (sspath == NULL) return 1;

    int result = remove(sspath);
    free(sspath);

    return result;
}

int emulation_sstate(int state) {
    FILE* state_file;
    char* sspath = get_savestate_path(state, true);
    if (sspath == NULL) return 1;

    state_file = fopen(sspath, "wb");
    free(sspath);
    if(state_file==NULL) {
//        alert("Error creating file!", "Check that you have permission to write", NULL, b_ok, NULL, 1, (int)NULL);
        return 1;
    }

    WORD size;

    #define FWRITE(buffer, size, count, stream) if (fwrite(buffer, size, count, stream) < (count)) goto bail;
    #define WRITE_VAR(V) FWRITE(&V, 1, sizeof(V), state_file)

    // Write header
    WORD id = 0x53535652; // "RVSS"
    WORD ver = SAVESTATE_VER; // version number
    WRITE_VAR(id);
    WRITE_VAR(ver);
    WRITE_VAR(tVBOpt.CRC32); // CRC32

    // Write registers
    WRITE_VAR(vb_state->v810_state.P_REG);
    WRITE_VAR(vb_state->v810_state.S_REG);
    WRITE_VAR(vb_state->v810_state.PC);
    WRITE_VAR(vb_state->v810_state.cycles);
    WRITE_VAR(vb_state->v810_state.except_flags);

    // Write VIP registers
    size = sizeof(vb_state->tVIPREG);
    WRITE_VAR(size);
    WRITE_VAR(vb_state->tVIPREG);

    // Write hardware control registers
    size = sizeof(vb_state->tHReg);
    WRITE_VAR(size);
    WRITE_VAR(vb_state->tHReg);

    // Write audio registers
    size = sizeof(sound_state);
    WRITE_VAR(size);
    WRITE_VAR(sound_state);

    // Write RAM
    #define WRITE_MEMORY(area) \
        size = vb_state->area.highaddr + 1 - vb_state->area.lowaddr; \
        WRITE_VAR(size); \
        FWRITE(vb_state->area.pmemory, 1, size, state_file);
    WRITE_MEMORY(V810_DISPLAY_RAM);
    WRITE_MEMORY(V810_SOUND_RAM);
    WRITE_MEMORY(V810_VB_RAM);
    WRITE_MEMORY(V810_GAME_RAM);
    #undef WRITE_MEMORY
    #undef WRITE_VAR
    #undef FWRITE

    fclose(state_file);
    return 0;

    bail:
    fclose(state_file);
    return 1;
}

int emulation_lstate(int state) {
    int i;
    int ret;
    uint32_t size;
    uint32_t id,ver,crc;
    FILE* state_file;
    char* sspath = get_savestate_path(state, false);
    if (sspath == NULL) return 1;

    state_file = fopen(sspath, "rb");
    free(sspath);

    if(state_file==NULL) {
        return 1;
    }

    #define FREAD(buffer,size,count,stream) if (fread(buffer,size,count,stream) < (count)) goto bail;

    // Load header
    FREAD(&id, 4, 1, state_file);
    FREAD(&ver, 4, 1, state_file);
    if ((id != 0x53535652) || (ver != 1 && ver != SAVESTATE_VER)) {
        goto bail;
    }
    FREAD(&crc, 4, 1, state_file);
    if (crc != tVBOpt.CRC32) {
        goto bail;
    }

    //Load registers
    cpu_state new_state = vb_state->v810_state;
    #define READ_VAR(V) FREAD(&V, 1, sizeof(V), state_file)
    READ_VAR(new_state.P_REG);
    READ_VAR(new_state.S_REG);
    READ_VAR(new_state.PC);
    READ_VAR(new_state.cycles);
    READ_VAR(new_state.except_flags);

    //Load VIP registers
    V810_VIPREGDAT new_vipreg = {0};
    READ_VAR(size);
    
    if (ver < 2) {
        if (size == 64) {
            // support <=0.9.6 savestates
            READ_VAR(new_vipreg.INTPND);
            READ_VAR(new_vipreg.INTENB);
            READ_VAR(new_vipreg.INTCLR);
            READ_VAR(new_vipreg.DPSTTS);
            READ_VAR(new_vipreg.DPCTRL);
            READ_VAR(new_vipreg.BRTA);
            READ_VAR(new_vipreg.BRTB);
            READ_VAR(new_vipreg.BRTC);
            READ_VAR(new_vipreg.REST);
            READ_VAR(new_vipreg.FRMCYC);
            READ_VAR(new_vipreg.XPSTTS);
            READ_VAR(new_vipreg.XPCTRL);
            READ_VAR(new_vipreg.tDisplayedFB);
            READ_VAR(new_vipreg.tFrame);
            READ_VAR(new_vipreg.SPT);
            READ_VAR(new_vipreg.GPLT);
            READ_VAR(new_vipreg.JPLT);
            READ_VAR(new_vipreg.BKCOL);
            u16 padding;
            READ_VAR(padding);
            READ_VAR(new_vipreg.lastdisp);
            READ_VAR(new_vipreg.rowcount);
            READ_VAR(new_vipreg.displaying);
            READ_VAR(new_vipreg.newframe);
            u8 padding2;
            READ_VAR(padding2);
            new_vipreg.frametime = 137216;
            new_vipreg.drawing = false;
            new_vipreg.lastdraw = new_vipreg.lastdisp;
        } else if (size == 68) {
            // support <=0.9.8 savestates
            READ_VAR(new_vipreg.INTPND);
            READ_VAR(new_vipreg.INTENB);
            READ_VAR(new_vipreg.INTCLR);
            READ_VAR(new_vipreg.DPSTTS);
            READ_VAR(new_vipreg.DPCTRL);
            READ_VAR(new_vipreg.BRTA);
            READ_VAR(new_vipreg.BRTB);
            READ_VAR(new_vipreg.BRTC);
            READ_VAR(new_vipreg.REST);
            READ_VAR(new_vipreg.FRMCYC);
            READ_VAR(new_vipreg.XPSTTS);
            READ_VAR(new_vipreg.XPCTRL);
            READ_VAR(new_vipreg.tDisplayedFB);
            READ_VAR(new_vipreg.tFrame);
            READ_VAR(new_vipreg.SPT);
            READ_VAR(new_vipreg.GPLT);
            READ_VAR(new_vipreg.JPLT);
            READ_VAR(new_vipreg.BKCOL);
            READ_VAR(new_vipreg.frametime);
            READ_VAR(new_vipreg.lastdisp);
            READ_VAR(new_vipreg.rowcount);
            READ_VAR(new_vipreg.drawing);
            READ_VAR(new_vipreg.displaying);
            READ_VAR(new_vipreg.newframe);
            new_vipreg.lastdraw = new_vipreg.lastdisp;
        } else if (size == 72) {
            // support <=1.0.2 savestates
            READ_VAR(new_vipreg.INTPND);
            READ_VAR(new_vipreg.INTENB);
            READ_VAR(new_vipreg.INTCLR);
            READ_VAR(new_vipreg.DPSTTS);
            READ_VAR(new_vipreg.DPCTRL);
            READ_VAR(new_vipreg.BRTA);
            READ_VAR(new_vipreg.BRTB);
            READ_VAR(new_vipreg.BRTC);
            READ_VAR(new_vipreg.REST);
            READ_VAR(new_vipreg.FRMCYC);
            READ_VAR(new_vipreg.XPSTTS);
            READ_VAR(new_vipreg.XPCTRL);
            READ_VAR(new_vipreg.tDisplayedFB);
            READ_VAR(new_vipreg.tFrame);
            READ_VAR(new_vipreg.SPT);
            HWORD gplt[4];
            READ_VAR(gplt);
            for (int i = 0; i < 4; i++) {
                new_vipreg.GPLT[i] = gplt[i];
            }
            HWORD jplt[4];
            READ_VAR(jplt);
            for (int i = 0; i < 4; i++) {
                new_vipreg.JPLT[i] = jplt[i];
            }
            READ_VAR(new_vipreg.BKCOL);
            READ_VAR(new_vipreg.frametime);
            READ_VAR(new_vipreg.lastdisp);
            READ_VAR(new_vipreg.lastdraw);
            READ_VAR(new_vipreg.rowcount);
            READ_VAR(new_vipreg.drawing);
            READ_VAR(new_vipreg.displaying);
            READ_VAR(new_vipreg.newframe);
        }
        new_vipreg.tDisplayedFB %= 2;
        // in version 2, modifying tDisplayedFB was moved to end-of-frame instead of start-of-frame
        // also, tFrame was inverted
        if (new_vipreg.tFrame >= new_vipreg.FRMCYC && !new_vipreg.drawing && (new_vipreg.XPCTRL & XPEN)) {
            new_vipreg.tDisplayedFB = !new_vipreg.tDisplayedFB;
            new_vipreg.tFrame = 0;
        } else {
            new_vipreg.tFrame = new_vipreg.FRMCYC - new_vipreg.tFrame;
        }
    } else if (size == sizeof(new_vipreg)) {
        static_assert(sizeof(new_vipreg) == 64, "VIPREG changed size");
        READ_VAR(new_vipreg);
    } else {
        goto bail;
    }
    //Validity checks
    if (new_vipreg.tDisplayedFB > 2 ||
        new_vipreg.rowcount > 0x21
    ) {
        goto bail;
    }

    //Load hardware control registers
    V810_HREGDAT new_hreg = {0};
    READ_VAR(size);
    if (size == 28) {
        // support <=0.9.7 states
        READ_VAR(new_hreg.SCR);
        READ_VAR(new_hreg.WCR);
        READ_VAR(new_hreg.TCR);
        READ_VAR(new_hreg.THB);
        READ_VAR(new_hreg.TLB);
        READ_VAR(new_hreg.ticks);
        READ_VAR(new_hreg.tTHW);
        READ_VAR(new_hreg.lasttime);
        READ_VAR(new_hreg.lastinput);
        READ_VAR(new_hreg.tCount);
        READ_VAR(new_hreg.SHB);
        READ_VAR(new_hreg.SLB);
        READ_VAR(new_hreg.CDRR);
        READ_VAR(new_hreg.CDTR);
        READ_VAR(new_hreg.CCSR);
        READ_VAR(new_hreg.CCR);
        READ_VAR(new_hreg.hwRead);
    } else if (size == sizeof(new_hreg)) {
        static_assert(sizeof(new_hreg) == 32, "HReg changed size");
        READ_VAR(new_hreg);
    } else {
        goto bail;
    }
    //Validity checks
    if (new_hreg.ticks >= 5) {
        goto bail;
    }

    //Load audio registers
    SOUND_STATE new_soundstate = {0};
    READ_VAR(size);
    if (size != sizeof(new_soundstate)) {
        goto bail;
    }
    static_assert(sizeof(new_soundstate) == 64, "Sound state changed size");
    READ_VAR(new_soundstate);
    //Validity checks
    if (new_soundstate.modulation_counter > 32) goto bail;
    for (int i = 0; i < 6; i++) {
        if (new_soundstate.channels[i].sample_pos >= 32) goto bail;
    }

    //Load the RAM (into player 2 at first)
    #define READ_MEMORY(area) \
        FREAD(&size, 4, 1, state_file); \
        if (size != vb_state->area.highaddr + 1 - vb_state->area.lowaddr) goto bail; \
        FREAD(vb_players[1].area.pmemory, 1, size, state_file);
    READ_MEMORY(V810_DISPLAY_RAM);
    READ_MEMORY(V810_SOUND_RAM);
    READ_MEMORY(V810_VB_RAM);
    READ_MEMORY(V810_GAME_RAM);
    #undef READ_MEMORY
    #undef READ_VAR

    fgetc(state_file); // set eof flag
    if (!feof(state_file)) goto bail;

    fclose(state_file);

    // Everything was loaded safely, now apply (player 2 to player 1)
    memcpy(&vb_state->v810_state, &new_state, sizeof(new_state));
    memcpy(&vb_state->tVIPREG, &new_vipreg, sizeof(new_vipreg));
    memcpy(&vb_state->tHReg, &new_hreg, sizeof(new_hreg));
    memcpy(&sound_state, &new_soundstate, sizeof(new_soundstate));
    BYTE *tmp;
    #define APPLY_MEMORY(area) \
        tmp = vb_players[0].area.pmemory; \
        vb_players[0].area.pmemory = vb_players[1].area.pmemory; \
        vb_players[1].area.pmemory = tmp; \
        vb_state->area.off = (size_t)vb_state->area.pmemory - vb_state->area.lowaddr;
    APPLY_MEMORY(V810_DISPLAY_RAM);
    APPLY_MEMORY(V810_SOUND_RAM);
    APPLY_MEMORY(V810_VB_RAM);
    APPLY_MEMORY(V810_GAME_RAM);
    #undef APPLY_MEMORY

    // frametime was moved to end-of-frame in version 2
    if (ver < 2) {
        if (!tVBOpt.VIP_OVERCLOCK) {
            vb_state->tVIPREG.frametime = videoProcessingTime();
        } else {
            // pre-0.9.7 behaviour
            vb_state->tVIPREG.frametime = 137216;
        }
    }

    clearCache();
    C3D_FrameBegin(0);
    video_render((vb_state->tVIPREG.tDisplayedFB) % 2, false);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
    C3D_FrameEnd(0);

    guiop = AKILL;
    return 0;

    bail:
    fclose(state_file);
    return 1;
}
#endif
