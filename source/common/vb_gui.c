#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

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
#include "rom_db.h"
#include "drc_core.h"
#include "main.h"

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
    fwrite(sram, 1, V810_GAME_RAM.highaddr + 1 - V810_GAME_RAM.lowaddr, nvramf);
    fclose(nvramf);

    free(sram);

    save_thread = NULL;
}

void save_sram(void) {
    // Do not save sram if it's not required!
    if (!is_sram) return;
    // Don't save if we're already saving
    if (save_thread) return;
    void *sram_copy = malloc(V810_GAME_RAM.highaddr + 1 - V810_GAME_RAM.lowaddr);
    memcpy(sram_copy, V810_GAME_RAM.pmemory, V810_GAME_RAM.highaddr + 1 - V810_GAME_RAM.lowaddr);
#ifdef __3DS__
    // Saving on the same thread is slow, but saving in another thread happens instantly
    APT_SetAppCpuTimeLimit(30);
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
    const int MAX_PATH_LEN = 300;
    char *sspath = (char *) malloc(MAX_PATH_LEN * sizeof(char));
    snprintf(sspath, MAX_PATH_LEN, "sdmc:/red-viper/savestates/%s", last_slash + 1);
    int sspath_len = strlen(sspath);
    char *end = strrchr(sspath, '.');
    if (!end) end = sspath + strlen(sspath);
    if (end - sspath + 20 >= MAX_PATH_LEN) goto bail;
    *end = 0;
    if (stat("sdmc:/red-viper", &st) == -1) {
        if (!write || mkdir("sdmc:/red-viper", 0777)) goto bail;
    }
    if (stat("sdmc:/red-viper/savestates", &st) == -1) {
        if (!write || mkdir("sdmc:/red-viper/savestates", 0777)) goto bail;
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
    WRITE_VAR(v810_state->P_REG);
    WRITE_VAR(v810_state->S_REG);
    WRITE_VAR(v810_state->PC);
    WRITE_VAR(v810_state->cycles);
    WRITE_VAR(v810_state->except_flags);

    // Write VIP registers
    size = sizeof(tVIPREG);
    WRITE_VAR(size);
    WRITE_VAR(tVIPREG);

    // Write hardware control registers
    size = sizeof(tHReg);
    WRITE_VAR(size);
    WRITE_VAR(tHReg);

    // Write audio registers
    size = sizeof(sound_state);
    WRITE_VAR(size);
    WRITE_VAR(sound_state);

    // Write RAM
    #define WRITE_MEMORY(area) \
        size = area.highaddr + 1 - area.lowaddr; \
        WRITE_VAR(size); \
        FWRITE(area.pmemory, 1, size, state_file);
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
    if ((id != 0x53535652) || (ver != SAVESTATE_VER)) {
        goto bail;
    }
    FREAD(&crc, 4, 1, state_file);
    if (crc != tVBOpt.CRC32) {
        goto bail;
    }

    //Load registers
    cpu_state new_state = *v810_state;
    #define READ_VAR(V) FREAD(&V, 1, sizeof(V), state_file)
    READ_VAR(new_state.P_REG);
    READ_VAR(new_state.S_REG);
    READ_VAR(new_state.PC);
    READ_VAR(new_state.cycles);
    READ_VAR(new_state.except_flags);

    //Load VIP registers
    V810_VIPREGDAT new_vipreg;
    READ_VAR(size);
    if (size != sizeof(new_vipreg)) {
        goto bail;
    }
    READ_VAR(new_vipreg);
    //Validity checks
    if (new_vipreg.tFrameBuffer > 2 ||
        new_vipreg.rowcount > 0x21
    ) {
        goto bail;
    }

    //Load hardware control registers
    V810_HREGDAT new_hreg;
    READ_VAR(size);
    if (size != sizeof(new_hreg)) {
        goto bail;
    }
    READ_VAR(new_hreg);
    //Validity checks
    if (new_hreg.ticks >= 5) {
        goto bail;
    }

    //Load audio registers
    SOUND_STATE new_soundstate;
    READ_VAR(size);
    if (size != sizeof(new_soundstate)) {
        goto bail;
    }
    READ_VAR(new_soundstate);
    //Validity checks
    if (new_soundstate.modulation_counter > 32) goto bail;
    for (int i = 0; i < 6; i++) {
        if (new_soundstate.channels[i].sample_pos >= 32) goto bail;
    }

    //Load the RAM
    #define READ_MEMORY(area) \
        FREAD(&size, 4, 1, state_file); \
        if (size != area.highaddr + 1 - area.lowaddr) goto bail; \
        FREAD(area.pbackup, 1, size, state_file);
    READ_MEMORY(V810_DISPLAY_RAM);
    READ_MEMORY(V810_SOUND_RAM);
    READ_MEMORY(V810_VB_RAM);
    READ_MEMORY(V810_GAME_RAM);
    #undef READ_MEMORY
    #undef READ_VAR

    fgetc(state_file); // set eof flag
    if (!feof(state_file)) goto bail;

    fclose(state_file);

    // Everything was loaded safely, now apply
    memcpy(v810_state, &new_state, sizeof(new_state));
    memcpy(&tVIPREG, &new_vipreg, sizeof(new_vipreg));
    memcpy(&tHReg, &new_hreg, sizeof(new_hreg));
    memcpy(&sound_state, &new_soundstate, sizeof(new_soundstate));
    BYTE *tmp;
    #define APPLY_MEMORY(area) \
        tmp = area.pmemory; \
        area.pmemory = area.pbackup; \
        area.pbackup = tmp; \
        area.off = (size_t)area.pmemory - area.lowaddr;
    APPLY_MEMORY(V810_DISPLAY_RAM);
    APPLY_MEMORY(V810_SOUND_RAM);
    APPLY_MEMORY(V810_VB_RAM);
    APPLY_MEMORY(V810_GAME_RAM);
    #undef APPLY_MEMORY

    clearCache();
    C3D_FrameBegin(0);
    video_render((tVIPREG.tFrameBuffer) % 2);
    C3D_FrameEnd(0);

    guiop = AKILL;
    return 0;

    bail:
    fclose(state_file);
    return 1;
}
