#ifndef VB_SET_H_
#define VB_SET_H_

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

#define CONFIG_FILENAME "rd_config.ini"

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
    char *ROM_NAME; // Path\Name of game to open
    char *PROG_NAME; // Path\Name of program
    unsigned long CRC32; // CRC32 of ROM
} VB_OPT;

void setDefaults(void);
int loadFileOptions(void);
int saveFileOptions(void);

extern VB_OPT tVBOpt;
extern int vbkey[15];

#endif
