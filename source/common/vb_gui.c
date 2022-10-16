#include <stdio.h>
#include <unistd.h>

#ifdef __3DS__
#include <3ds.h>
#endif

#include "vb_types.h"
#include "v810_cpu.h"
#include "v810_mem.h"
#include "vb_gui.h"
#include "vb_set.h"
#include "rom_db.h"
#include "drc_core.h"

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

menu_item_t main_menu_items[] = {
    {"File", NULL, &file_menu, 0, NULL},
    {"Options", NULL, &options_menu, 0, NULL},
    {"Emulation", NULL, &emulation_menu, D_DISABLED, NULL},
    {"Debug", NULL, &debug_menu, D_DISABLED, NULL},
    {"Help", NULL, &help_menu, 0, NULL}
};

menu_item_t file_menu_items[] = {
    {"Load ROM", file_loadrom, NULL, 0, NULL},
    {"Close ROM", file_closerom, NULL, D_DISABLED, NULL},
    {"Exit", file_exit, NULL, 0, NULL}
};

menu_item_t options_menu_items[] = {
    {"Max cycles", options_maxcycles, NULL, 0, NULL},
    {"Frameskip", options_frameskip, NULL, 0, NULL},
    {"Toggle debug", options_debug, NULL, 0, NULL},
    {"Toggle sound", options_sound, NULL, 0, NULL},
    {"Input", options_input, NULL, 0, NULL},
};

menu_item_t emulation_menu_items[] = {
    {"Resume Emulation", emulation_resume, NULL, 0, NULL},
    {"Reset System", emulation_reset, NULL, 0, NULL},
    {"Cheats", NULL, &emulation_cheat_menu, 0, NULL},
    {"Save State", emulation_sstate, NULL, 0, NULL},
    {"Load State", emulation_lstate, NULL, 0, NULL}
};

menu_item_t debug_menu_items[] = {
    {"Trace Logging", debug_trace, NULL, 0, NULL},
    {"Show ROM Info", debug_showinfo, NULL, 0, NULL},
    {"Show Dump Info", debug_dumpinfo, NULL, 0, NULL},
    {"View", NULL, &debug_view_menu,0, NULL},
    {"Watch Points", debug_watchpoints, NULL, 0, NULL},
    {"Write World\\VIP Info", debug_write_info, NULL, 0, NULL},
    {"Write Affine Info", debug_write_affine, NULL, 0, NULL},
    {"Dump VB RAM to file", debug_dumpvbram, NULL, 0, NULL},
    {"Dump game RAM to file", debug_dumpgameram, NULL, 0, NULL},
    {"Dump dynarec cache", debug_dumpdrccache, NULL, 0, NULL}
};

menu_item_t debug_view_menu_items[] = {
    {"Memory", debug_view_memory, NULL, 0, NULL},
    {"Chars", debug_view_chars, NULL, 0, NULL},
    {"BGMaps", debug_view_bgmaps, NULL, 0, NULL},
    {"Worlds", debug_view_worlds, NULL, 0, NULL},
    {"OBJs", debug_view_obj, NULL, 0, NULL}
};

menu_item_t emulation_cheat_menu_items[] = {
    {"Browse Cheats", debug_cheat_browse, NULL, 0, NULL},
    {"Search (Exact)", debug_cheat_search_exact, NULL, 0, NULL},
    {"Search (Comparative)", debug_cheat_search_comp, NULL, 0, NULL},
    {"View Results", debug_cheat_view, NULL, 0, NULL}
};

menu_item_t help_menu_items[] = {
    {"About", help_about, &help_menu, 0, NULL}
};

menu_t main_menu = {
    "Main menu",
    NULL,
    LENGTH(main_menu_items),
    main_menu_items
};

menu_t file_menu = {
    "File",
    &main_menu,
    LENGTH(file_menu_items),
    file_menu_items
};

menu_t options_menu  = {
    "Options",
    &main_menu,
    LENGTH(options_menu_items),
    options_menu_items
};

menu_t emulation_menu = {
    "Emulation",
    &main_menu,
    LENGTH(emulation_menu_items),
    emulation_menu_items
};

menu_t debug_menu = {
    "Debug",
    &main_menu,
    LENGTH(debug_menu_items),
    debug_menu_items
};

menu_t debug_view_menu = {
    "Debug view",
    &debug_menu,
    LENGTH(debug_view_menu_items),
    debug_view_menu_items
};

menu_t emulation_cheat_menu = {
    "Cheats",
    &emulation_menu,
    LENGTH(emulation_cheat_menu_items),
    emulation_cheat_menu_items
};

menu_t help_menu = {
    "Help",
    &main_menu,
    LENGTH(help_menu_items),
    help_menu_items
};

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

void save_sram(void) {
    FILE *nvramf;
    char ram_name[131];

    // Do not save sram if it's not required!
    if (!is_sram) return;

    snprintf(ram_name, 131, "%s.ram", tVBOpt.ROM_NAME);

    // Save out the sram ram
    if((nvramf = fopen(ram_name, "wb")) == NULL) {
        printf("\nError opening sram.bin");
        return;
    }
    fwrite(V810_GAME_RAM.pmemory, 1, V810_GAME_RAM.highaddr - V810_GAME_RAM.lowaddr, nvramf);
    fclose(nvramf);
}

int file_loadrom(void) {
    int ret;
    char rompath[256];

    ret = fileSelect("Load ROM", rompath, "vb");
    // Go back if the menu was cancelled
    if (ret < 0)
        return D_OK;

    tVBOpt.ROM_NAME = rompath;
    guiop = AKILL | VBRESET;
//    cheat_reset_list();

    // Exit the menu if a file was selected
    return D_EXIT;
}

int file_closerom(void) {
//    if (debuglog) debug_trace(); //disable trace logging if enabled
    save_sram();
//    cheat_reset_list();
    // TODO: Actually close the ROM
    return D_EXIT; //redraw!
}

int file_exit(void) {
    guiop = AKILL | GUIEXIT;
    return D_EXIT;
}

int options_maxcycles(void) {
#ifdef __3DS__
    char buf[5] = "";
    SwkbdState swkbd;
    SwkbdButton button;

    sprintf(buf, "%d", tVBOpt.MAXCYCLES);

    swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 1, 4);
    swkbdSetInitialText(&swkbd, buf);
    swkbdSetHintText(&swkbd, "The max number of cycles before checking for interrupts");
    if (swkbdInputText(&swkbd, buf, sizeof(buf)) != SWKBD_BUTTON_CONFIRM)
        return D_OK;

    tVBOpt.MAXCYCLES = atoi(buf);
    
    saveFileOptions();
#endif
    return D_OK;
}

int options_frameskip(void) {
#ifdef __3DS__
    char buf[2] = "";
    SwkbdState swkbd;
    SwkbdButton button;

    sprintf(buf, "%d", tVBOpt.FRMSKIP);

    swkbdInit(&swkbd, SWKBD_TYPE_NUMPAD, 2, 1);
    swkbdSetInitialText(&swkbd, buf);
    swkbdSetHintText(&swkbd, "The number of frames to skip");
    if (swkbdInputText(&swkbd, buf, sizeof(buf)) != SWKBD_BUTTON_CONFIRM)
        return D_OK;

    tVBOpt.FRMSKIP = atoi(buf);
 
    saveFileOptions();
#endif
    return D_OK;
}

int options_debug(void) {
#ifdef __3DS__
    consoleClear();
    tVBOpt.DEBUG = !tVBOpt.DEBUG;
    printf("Debug output %s", tVBOpt.DEBUG ? "enabled" : "disabled");
    waitForInput();

    saveFileOptions();
#endif
    return D_OK;
}

int options_sound(void) {
#ifdef __3DS__
    consoleClear();
    tVBOpt.SOUND = !tVBOpt.SOUND;
    printf("Sound %s", tVBOpt.DEBUG ? "enabled" : "disabled");
    waitForInput();

    saveFileOptions();
#endif
    return D_OK;
}

int options_input(void) {
    // TODO: Implement me!
    GUI_STUB();
    return D_OK;
}

int emulation_resume(void) {
    // Do nothing
    return D_EXIT;
}

int emulation_reset(void) {
    // TODO: Implement VBRESET
    guiop = AKILL | VBRESET;
//    cheat_save_old_ram();
    return D_EXIT;
}

int emulation_sstate(void) {
    int i;
    int highbyte;
    int lowbyte;
    char sspath[131];
    FILE* state_file;

    sprintf(sspath, "%s.rds", tVBOpt.ROM_NAME);

    state_file = fopen(sspath, "wb");

    if(state_file==NULL) {
//        alert("Error creating file!", "Check that you have permission to write", NULL, b_ok, NULL, 1, (int)NULL);
        return 0;
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
    return D_EXIT;
}

int emulation_lstate(void) {
    int i;
    int ret;
    int id,ver,crc;
    char sspath[131];
    FILE* state_file;

    sprintf(sspath, "%s.rds", tVBOpt.ROM_NAME);

    state_file = fopen(sspath, "rb");

    if(state_file==NULL) {
//        alert("Error reading file!", "Check that it still exists", NULL, b_ok, NULL, 1, (int)NULL);
        return 0;
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
        return 0;
    }
    fread(&crc, 4, 1, state_file);
    if (crc != tVBOpt.CRC32) {
//        sprintf(str1, "Loaded crc32:   %08X",crc);
//        sprintf(str2, "Expected crc32: %08X",tVBOpt.CRC32);
//        if (alert("Invalid CRC32. Load anyway?", str1, str2, b_ok, b_cancel, KEY_ENTER, KEY_ESC) == 2)
        return 0;
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
    return D_EXIT;
}

int debug_trace(void) {
    // TODO: Implement me!
    GUI_STUB();
    return D_OK;
}

int debug_showinfo(void) {
    char romtitle[21];
    char s_mfg[32];
    int mfg;
    int version;
    char id[4];
    int i;

    for(i=0;i<20;i++) {
        romtitle[i] = ((char)mem_rbyte(0x07FFFDE0+i))&0xFF;
        //Get rid of any weird characers that might make the emu crash
        if( ((int)romtitle[i]<32) || ((int)romtitle[i]>126) ) { romtitle[i] = (char)32; }
        romtitle[20]= '\0';
    }

    mfg = (mem_rbyte(0x07FFFDF9)<<8) + mem_rbyte(0x07FFFDFA);

    strcpy(s_mfg, "Unknown");

    switch(mfg) {
        case 0x3031:
            strcpy(s_mfg, "Nintendo");
            break;
        case 0x3042:
            strcpy(s_mfg, "Coconuts Japan");
            break;
        case 0x3138:
            strcpy(s_mfg, "Hudson Soft");
            break;
        case 0x3238:
            strcpy(s_mfg, "Kemco");
            break;
        case 0x3637:
            strcpy(s_mfg, "Ocean");
            break;
        case 0x3842:
            strcpy(s_mfg, "Bullet Proof Software");
            break;
        case 0x3846:
            strcpy(s_mfg, "I'Max");
            break;
        case 0x3939:
            strcpy(s_mfg, "Pack in Video");
            break;
        case 0x4148:
            strcpy(s_mfg, "J-Wing");
            break;
        case 0x4232:
            strcpy(s_mfg, "Bandai");
            break;
        case 0x4330:
            strcpy(s_mfg, "Taito");
            break;
        case 0x4537:
            strcpy(s_mfg, "Athena");
            break;
        case 0x4542:
            strcpy(s_mfg, "Atlus");
            break;
        default:
            strcpy(s_mfg, "Unknown");
            break;
    }

    for(i = 0; i < 4; i++)
        id[i] = mem_rbyte(0x07FFFDFB+i);

    version = mem_rbyte(0x07FFFDFF);

    consoleClear();
    printf("\x1b[7mROM info\x1b[0m\n\n");
    printf("\x1b[1mTitle:\x1b[0m %s\n", romtitle);
    printf("\x1b[1mManufacturer:\x1b[0m 0x%04X (%s)\n", mfg, s_mfg);
    printf("\x1b[1mGame ID:\x1b[0m %c%c%c%c\n", id[0], id[1], id[2], id[3]);
    printf("\x1b[1mVersion:\x1b[0m 0x%02X\n", version);
    waitForInput();

    return D_OK;
}


int debug_dumpinfo(void) {
    int romnum = db_find(tVBOpt.CRC32);

    consoleClear();
    printf("\x1b[7mDump info\x1b[0m\n\n");
    printf("\x1b[1mCRC32:\x1b[0m %08lX\n", tVBOpt.CRC32);
    printf("\x1b[1mTitle:\x1b[0m %s\n", rom_db[romnum].title);
    // Prints the rom status either green (good) or red (bad)
    printf("\x1b[1mDump Status:\x1b[0m \x1b[3%cm%s\x1b[0m\n", rom_db[romnum].status[0] == 'G' ? '2' : '1', rom_db[romnum].status);
    waitForInput();

    return D_OK;
}

int debug_watchpoints(void) {
    // TODO: Implement me!
    GUI_STUB();
    return D_OK;
}

int debug_write_info(void) {
    // TODO: Implement me!
    GUI_STUB();
    return D_OK;
}

int debug_write_affine(void) {
    // TODO: Implement me!
    GUI_STUB();
    return D_OK;
}

int debug_dumpvbram(void) {
    FILE* f = fopen("vb_ram.bin", "w");
    fwrite(V810_VB_RAM.pmemory, V810_VB_RAM.highaddr - V810_VB_RAM.lowaddr,1, f);
    fclose(f);
    return D_OK;
}

int debug_dumpgameram(void) {
    FILE* f = fopen("game_ram.bin", "w");
    fwrite(V810_GAME_RAM.pmemory, V810_GAME_RAM.highaddr - V810_GAME_RAM.lowaddr,1, f);
    fclose(f);
    return D_OK;
}

int debug_dumpdrccache(void) {
    drc_dumpCache("cache_dump.bin");
    return D_OK;
}

int debug_view_memory(void) {
    // TODO: Implement me!
    GUI_STUB();
    return D_OK;
}

int debug_view_chars(void) {
    // TODO: Implement me!
    GUI_STUB();
    return D_OK;
}

int debug_view_bgmaps(void) {
    // TODO: Implement me!
    GUI_STUB();
    return D_OK;
}

int debug_view_worlds(void) {
    // TODO: Implement me!
    GUI_STUB();
    return D_OK;
}

int debug_view_obj(void) {
    // TODO: Implement me!
    GUI_STUB();
    return D_OK;
}

int debug_cheat_browse(void) {
    // TODO: Implement me!
    GUI_STUB();
    return D_OK;
}

int debug_cheat_search_exact(void) {
    // TODO: Implement me!
    GUI_STUB();
    return D_OK;
}

int debug_cheat_search_comp(void) {
    // TODO: Implement me!
    GUI_STUB();
    return D_OK;
}

int debug_cheat_view(void) {
    // TODO: Implement me!
    GUI_STUB();
    return D_OK;
}

int help_about(void) {
    consoleClear();
    printf("\x1b[7mr3Ddragon\x1b[0m\n\n");
    printf("Heavily based on Reality Boy by David\n");
    printf("Tucker.\n\n");
    printf("More info at:\n\x1b[1;4mgithub.com/mrdanielps/r3Ddragon\x1b[0m\n");
    waitForInput();

    return D_OK;
}
