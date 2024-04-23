#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef __3DS__
#include <3ds.h>
#endif

#include "vb_types.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_gui.h"
#include "vb_set.h"
#include "vb_sound.h"
#include "rom_db.h"
#include "drc_core.h"
#include "main.h"

#ifdef __3DS__
#define GUI_STUB() { \
    consoleClear(); \
    printf("STUBBED"); \
    waitForInput(); \
}
#else
#define GUI_STUB()
#define consoleClear()
#endif

struct stat st = {0};

u32 waitForInput() {
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

char * get_savestate_path(int state) {
    char * sspath;
    sspath = (char *) malloc(131 * sizeof(char));
    char file_name[8];
    char * last_slash = strrchr(tVBOpt.ROM_PATH, '/');
    strncpy(sspath, tVBOpt.ROM_PATH, last_slash - tVBOpt.ROM_PATH);
    sspath[last_slash - tVBOpt.ROM_PATH] = '\0';
    strcat(sspath, "/vb_savestates");
    if(stat(sspath, &st) == -1)
        mkdir(sspath, 0777);
    strcat(sspath, tVBOpt.ROM_PATH + (last_slash - tVBOpt.ROM_PATH));
    sspath[strlen(sspath) - 3] = '\0';
    if(stat(sspath, &st) == -1)
        mkdir(sspath, 0777);
    sprintf(file_name, "/%d.rds", state);
    strcat(sspath, file_name);

    return sspath;
}

int emulation_rmstate(int state) {
    char* sspath = get_savestate_path(state);

    int result = remove(sspath);
    free(sspath);

    return result;
}

int emulation_sstate(int state) {
    int i;
    int highbyte;
    int lowbyte;
    FILE* state_file;
    char* sspath = get_savestate_path(state);

    state_file = fopen(sspath, "wb");
    free(sspath);
    if(state_file==NULL) {
//        alert("Error creating file!", "Check that you have permission to write", NULL, b_ok, NULL, 1, (int)NULL);
        return 1;
    }

    // Write header
    putw(0x53534452, state_file); // "RDSS"
    putw(SAVESTATE_VER, state_file); // version number
    fwrite(&tVBOpt.CRC32, 4, 1, state_file); // CRC32

    // Write registers
    fwrite(v810_state->P_REG, 4, 32, state_file);
    fwrite(v810_state->S_REG, 4, 32, state_file);
    fwrite(&v810_state->PC, 4, 1, state_file);

    // Write the VIP registers first (could be done better!)
    fwrite(&tVIPREG.INTPND, 2, 1, state_file);
    fwrite(&tVIPREG.INTENB, 2, 1, state_file);
    fwrite(&tVIPREG.INTCLR, 2, 1, state_file);
    fwrite(&tVIPREG.DPSTTS, 2, 1, state_file);
    fwrite(&tVIPREG.DPCTRL, 2, 1, state_file);
    fwrite(&tVIPREG.BRTA, 2, 1, state_file);
    fwrite(&tVIPREG.BRTB, 2, 1, state_file);
    fwrite(&tVIPREG.BRTC, 2, 1, state_file);
    fwrite(&tVIPREG.REST, 2, 1, state_file);
    fwrite(&tVIPREG.FRMCYC, 2, 1, state_file);
    fwrite(&tVIPREG.CTA, 2, 1, state_file);
    fwrite(&tVIPREG.XPSTTS, 2, 1, state_file);
    fwrite(&tVIPREG.XPCTRL, 2, 1, state_file);
    fwrite(&tVIPREG.SPT[0], 2, 1, state_file);
    fwrite(&tVIPREG.SPT[1], 2, 1, state_file);
    fwrite(&tVIPREG.SPT[2], 2, 1, state_file);
    fwrite(&tVIPREG.SPT[3], 2, 1, state_file);
    fwrite(&tVIPREG.GPLT[0], 2, 1, state_file);
    fwrite(&tVIPREG.GPLT[1], 2, 1, state_file);
    fwrite(&tVIPREG.GPLT[2], 2, 1, state_file);
    fwrite(&tVIPREG.GPLT[3], 2, 1, state_file);
    fwrite(&tVIPREG.JPLT[0], 2, 1, state_file);
    fwrite(&tVIPREG.JPLT[1], 2, 1, state_file);
    fwrite(&tVIPREG.JPLT[2], 2, 1, state_file);
    fwrite(&tVIPREG.JPLT[3], 2, 1, state_file);
    fwrite(&tVIPREG.BKCOL, 2, 1, state_file);
    fwrite(&tVIPREG.tFrameBuffer, 2, 1, state_file); //not publicly visible
    fwrite(&tVIPREG.tFrame, 2, 1, state_file); //not publicly visible

    // Write hardware control registers
    for(i=0x02000000;i<=0x02000028;i=i+4) {
        fputc(hcreg_rbyte(i), state_file);
    }

    fwrite(&tHReg.tTHW, 2, 1, state_file);   //not publicly visible
    fwrite(&tHReg.tCount, 2, 1, state_file); //not publicly visible
    fwrite(&tHReg.tTRC, 4, 1, state_file);         //not publicly visible

    // Next we write all the RAM contents
    fwrite(V810_DISPLAY_RAM.pmemory, 1, V810_DISPLAY_RAM.highaddr - V810_DISPLAY_RAM.lowaddr, state_file);
    fwrite(V810_SOUND_RAM.pmemory, 1, V810_SOUND_RAM.highaddr - V810_SOUND_RAM.lowaddr, state_file);
    fwrite(V810_VB_RAM.pmemory, 1, V810_VB_RAM.highaddr - V810_VB_RAM.lowaddr, state_file);
    fwrite(V810_GAME_RAM.pmemory, 1, V810_GAME_RAM.highaddr - V810_GAME_RAM.lowaddr, state_file);

    fclose(state_file);
    return 0;
}

int emulation_lstate(int state) {
    int i;
    int ret;
    int id,ver,crc;
    FILE* state_file;
    char* sspath = get_savestate_path(state);

    state_file = fopen(sspath, "rb");
    free(sspath);

    if(state_file==NULL) {
//        alert("Error reading file!", "Check that it still exists", NULL, b_ok, NULL, 1, (int)NULL);
        return 1;
    }

    // fix for cheat searching!
//    cheat_save_old_ram();

    // Load header
    fread(&id, 4, 1, state_file);
    fread(&ver, 4, 1, state_file);
    if ((id != 0x53534452) || (ver != SAVESTATE_VER)) {
//        sprintf(str1, "ID:      %c%c%c%c",(id>>24),((id>>16)&0xFF),((id>>8)&0xFF),(id&0xFF));
//        sprintf(str2, "Version: %04x",ver);
//        if (alert("Invalid header. Load anyway?", str1, str2, b_ok, b_cancel, KEY_ENTER, KEY_ESC) == 2)
        return 1;
    }
    fread(&crc, 4, 1, state_file);
    if (crc != tVBOpt.CRC32) {
//        sprintf(str1, "Loaded crc32:   %08X",crc);
//        sprintf(str2, "Expected crc32: %08X",tVBOpt.CRC32);
//        if (alert("Invalid CRC32. Load anyway?", str1, str2, b_ok, b_cancel, KEY_ENTER, KEY_ESC) == 2)
        return 1;
    }

    //Load registers
    fread(v810_state->P_REG, 4, 32, state_file);
    fread(v810_state->S_REG, 4, 32, state_file);
    fread(&v810_state->PC, 4, 1, state_file);

    //Load VIP registers
    fread(&tVIPREG.INTPND, 2, 1, state_file);
    fread(&tVIPREG.INTENB, 2, 1, state_file);
    fread(&tVIPREG.INTCLR, 2, 1, state_file);
    fread(&tVIPREG.DPSTTS, 2, 1, state_file);
    fread(&tVIPREG.DPCTRL, 2, 1, state_file);
    fread(&tVIPREG.BRTA, 2, 1, state_file);
    fread(&tVIPREG.BRTB, 2, 1, state_file);
    fread(&tVIPREG.BRTC, 2, 1, state_file);
    fread(&tVIPREG.REST, 2, 1, state_file);
    fread(&tVIPREG.FRMCYC, 2, 1, state_file);
    fread(&tVIPREG.CTA, 2, 1, state_file);
    fread(&tVIPREG.XPSTTS, 2, 1, state_file);
    fread(&tVIPREG.XPCTRL, 2, 1, state_file);
    fread(&tVIPREG.SPT[0], 2, 1, state_file);
    fread(&tVIPREG.SPT[1], 2, 1, state_file);
    fread(&tVIPREG.SPT[2], 2, 1, state_file);
    fread(&tVIPREG.SPT[3], 2, 1, state_file);
    fread(&tVIPREG.GPLT[0], 2, 1, state_file);
    fread(&tVIPREG.GPLT[1], 2, 1, state_file);
    fread(&tVIPREG.GPLT[2], 2, 1, state_file);
    fread(&tVIPREG.GPLT[3], 2, 1, state_file);
    fread(&tVIPREG.JPLT[0], 2, 1, state_file);
    fread(&tVIPREG.JPLT[1], 2, 1, state_file);
    fread(&tVIPREG.JPLT[2], 2, 1, state_file);
    fread(&tVIPREG.JPLT[3], 2, 1, state_file);
    fread(&tVIPREG.BKCOL, 2, 1, state_file);
    fread(&tVIPREG.tFrameBuffer, 2, 1, state_file); //not publicly visible
    fread(&tVIPREG.tFrame, 2, 1, state_file); //not publicly visible

    //Load hardware control registers
    for(i=0x02000000;i<=0x02000028;i=i+4) {
        hcreg_wbyte(i, fgetc(state_file));
    }
    fread(&tHReg.tTHW, 2, 1, state_file); //not publicly visible
    fread(&tHReg.tCount, 2, 1, state_file); //not publicly visible
    fread(&tHReg.tTRC, 4, 1, state_file); //not publicly visible

    //Load the RAM
    fread(V810_DISPLAY_RAM.pmemory, 1, V810_DISPLAY_RAM.highaddr - V810_DISPLAY_RAM.lowaddr, state_file);
    fread(V810_SOUND_RAM.pmemory, 1, V810_SOUND_RAM.highaddr - V810_SOUND_RAM.lowaddr, state_file);
    fread(V810_VB_RAM.pmemory, 1, V810_VB_RAM.highaddr - V810_VB_RAM.lowaddr, state_file);
    fread(V810_GAME_RAM.pmemory, 1, V810_GAME_RAM.highaddr - V810_GAME_RAM.lowaddr, state_file);

    fclose(state_file);

    guiop = AKILL;
    return 0;
}
