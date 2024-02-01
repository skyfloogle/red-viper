#ifndef VB_GUI_H
#define VB_GUI_H

#include "3ds.h"
#include "allegro_compat.h"

#define AKILL           0x01
#define GUISTATUS       0x02
#define GUIEXIT         0x04
#define VBRESET         0x08

#define SAVESTATE_VER   0x00000001

#define LENGTH(array) (sizeof(array)/sizeof(array[0]))

int guiop;

u32 waitForInput();
void save_sram(void);
int file_loadrom(void);
int file_closerom(void);
int file_exit(void);
int options_maxcycles(void);
int options_frameskip(void);
int options_debug(void);
int options_sound(void);
int options_fastforward(void);
int options_legacyrender(void);
int options_input(void);
int options_saveoptions(void);
int emulation_resume(void);
int emulation_reset(void);
int emulation_sstate(void);
int emulation_lstate(void);
int debug_trace(void);
int debug_showinfo(void);
int debug_dumpinfo(void);
int debug_watchpoints(void);
int debug_write_info(void);
int debug_write_affine(void);
int debug_dumpvbram(void);
int debug_dumpgameram(void);
int debug_dumpdrccache(void);
int debug_view_memory(void);
int debug_view_chars(void);
int debug_view_bgmaps(void);
int debug_view_worlds(void);
int debug_view_obj(void);
int debug_cheat_browse(void);
int debug_cheat_search_exact(void);
int debug_cheat_search_comp(void);
int debug_cheat_view(void);
int help_about(void);

void guiInit();
void guiUpdate();
void openMenu(bool);

bool guiShouldPause();

#endif //VB_GUI_H
