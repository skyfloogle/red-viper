#ifndef VB_SET_H_
#define VB_SET_H_

#include "3ds/types.h"

#define D_VGA       0
#define D_VESA1     1
#define D_VESA2     2

#define DM_NORMAL   0
#define DM_3D       1

#define PAL_NORMAL  0
#define PAL_RED     1
#define PAL_RB      2
#define PAL_RG      3
#define PAL_RBG     4

#define SLIDER_3DS  0
#define SLIDER_VB   1

#define CONFIG_FILENAME "rv_config.ini"

// vbkey positions
enum VB_KCFG {
    VB_KCFG_LUP,
    VB_KCFG_LDOWN,
    VB_KCFG_LLEFT,
    VB_KCFG_LRIGHT,
    VB_KCFG_RUP,
    VB_KCFG_RDOWN,
    VB_KCFG_RLEFT,
    VB_KCFG_RRIGHT,
    VB_KCFG_A,
    VB_KCFG_B,
    VB_KCFG_START,
    VB_KCFG_SELECT,
    VB_KCFG_L,
    VB_KCFG_R,
};

// Global Options list
typedef struct VB_OPT {
    int   MAXCYCLES; // Number of cycles before checking for interrupts
    int   FRMSKIP;  // Frame Skip of course
    int   DSPMODE;  // Normal, 3D, etc
    int   DSPSWAP;  // Swap 3D effect, 0 normal, 1 swap
    int   DSP2X;    // Double screen size
    int   TINT;     // Colour tint
    int   PALMODE;  // Select a palette Type: 0-normal, 1-red, etc...
    int   DEBUG;    // Release vs Debug
    int   STDOUT;   // File vs Screen (debug)
    int   BFACTOR;  // User adjustable brightness (not implemented)
    int   SCR_X;    // Display dimensions
    int   SCR_Y;
    int   FIXPAL;   // Use a fixed pallet, no brightness...
    int   DISASM;   // Interactive disassembly of all executed instructions...
    int   SCR_MODE; // 0-VGA, 1-VESA1, 2-VESA2
    int   SOUND;
    int   DYNAREC;
    int   FASTFORWARD;
    int   RENDERMODE; // 0 - hard only, 1 - hard + postproc, 2 - full soft
    int   SLIDERMODE; // 0 - 3ds (positive parallax), 1 - virtual boy (full parallax)
    int   PAUSE_RIGHT; // right side of pause block on touch screen
    int   TOUCH_AX;
    int   TOUCH_AY;
    int   TOUCH_BX;
    int   TOUCH_BY;
    int   TOUCH_PADX;
    int   TOUCH_PADY;
    int   ABXY_MODE; // 0: A=A B=B, 1: B=A Y=B, 2: A=B B=A, 3: B=B Y=A
    char *ROM_PATH;
    char *RAM_PATH;
    char *ROM_NAME; // Path\Name of game to open
    char *PROG_NAME; // Path\Name of program
    unsigned long CRC32; // CRC32 of ROM
    bool  PERF_INFO;
} VB_OPT;

void setDefaults(void);
int loadFileOptions(void);
int saveFileOptions(void);

extern VB_OPT tVBOpt;
extern int vbkey[32];

#endif
