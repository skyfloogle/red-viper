#ifndef VB_SET_H_
#define VB_SET_H_

#include <stdbool.h>

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

#define CONFIG_FILENAME "sdmc:/config/red-viper/rv_config.ini"
#define CONFIG_FILENAME_LEGACY "rv_config.ini"

#define INPUT_BUFFER_MAX 10

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

typedef enum {
    RM_GPUONLY, // VIP only.
    RM_TOGPU, // Mix CPU and hardware VIP graphics on GPU.
    RM_CPUONLY, // Software VIP.
} RENDERMODE_t;

// Global Options list
typedef struct VB_OPT {
    int   MAXCYCLES; // Number of cycles before checking for interrupts
    int   FRMSKIP;  // Frame Skip of course
    int   DSPMODE;  // Normal, 3D, etc
    int   DSPSWAP;  // Swap 3D effect, 0 normal, 1 swap
    int   DSP2X;    // Double screen size
    bool  MULTICOL; // Multicolour toggle
    int   TINT;     // Colour tint
    int   MULTIID;  // Multicolour ID
    int   MTINT[4][4]; // Multicolour tints
    float STINT[4][3]; // Tint scale
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
    int   FASTFORWARD;
    int   FF_TOGGLE; // 0 - hold, 1 - toggle
    RENDERMODE_t RENDERMODE;
    int   SLIDERMODE; // 0 - 3ds (positive parallax), 1 - virtual boy (full parallax)
    int   DEFAULT_EYE; // 0 - left, 1 - right
    int   PAUSE_RIGHT; // right side of pause block on touch screen
    int   TOUCH_AX;
    int   TOUCH_AY;
    int   TOUCH_BX;
    int   TOUCH_BY;
    int   TOUCH_PADX;
    int   TOUCH_PADY;
    int   TOUCH_BUTTONS; // 0: Right D-Pad, 1: Buttons
    bool  TOUCH_SWITCH; // true: enabled, false: disabled
    bool  CPP_ENABLED;
    int   ABXY_MODE; // 0: A=A B=B, 1: B=A Y=B, 2: A=B B=A, 3: B=B Y=A
    int   ZLZR_MODE; // 0: ZL=B ZR=A, 1: ZL=A, ZR=B, 2: L=B R=A, 3: L=A R=B
    int   DPAD_MODE; // Left 3DS DPAD Behavior: 0: VB LPAD, 1: VB RPAD, 2: Mirror ABXY buttons
    char ROM_PATH[300];
    char RAM_PATH[300];
    char LAST_ROM[300];
    int   CUSTOM_CONTROLS; // 0 - preset control scheme, 1 - custom control scheme
    int   CUSTOM_MAPPING_DUP; // These are all 3DS buttons, each set/mapped to a VB_* value from vb_dsp.h
    int   CUSTOM_MAPPING_DDOWN;
    int   CUSTOM_MAPPING_DLEFT;
    int   CUSTOM_MAPPING_DRIGHT;
    int   CUSTOM_MAPPING_CPAD_UP;
    int   CUSTOM_MAPPING_CPAD_DOWN;
    int   CUSTOM_MAPPING_CPAD_LEFT;
    int   CUSTOM_MAPPING_CPAD_RIGHT;
    int   CUSTOM_MAPPING_CSTICK_UP;
    int   CUSTOM_MAPPING_CSTICK_DOWN;
    int   CUSTOM_MAPPING_CSTICK_LEFT;
    int   CUSTOM_MAPPING_CSTICK_RIGHT;
    int   CUSTOM_MAPPING_A;
    int   CUSTOM_MAPPING_X;
    int   CUSTOM_MAPPING_B;
    int   CUSTOM_MAPPING_Y;
    int   CUSTOM_MAPPING_START;
    int   CUSTOM_MAPPING_SELECT;
    int   CUSTOM_MAPPING_L;
    int   CUSTOM_MAPPING_R;
    int   CUSTOM_MAPPING_ZL;
    int   CUSTOM_MAPPING_ZR;
    int   CUSTOM_MOD[32]; // 0: default, 1: toggle, 2: turbo
    char *ROM_NAME; // Path\Name of game to open
    char *PROG_NAME; // Path\Name of program
    char HOME_PATH[240];
    unsigned long CRC32; // CRC32 of ROM
    char  GAME_ID[6];
    bool  PERF_INFO;
    bool  VSYNC;
    bool  N3DS_SPEEDUP;
    bool  ANAGLYPH;
    int   ANAGLYPH_LEFT;
    int   ANAGLYPH_RIGHT;
    int   ANAGLYPH_DEPTH;
    bool  GAME_SETTINGS; // Are we using game-specific settings?
    bool  MODIFIED; // Do we need to ask for save?
    bool  INPUTS; // Input display
    bool  ANTIFLICKER;
    bool  VIP_OVERCLOCK;
    bool  VIP_OVER_SOFT;
    bool  FORWARDER;
    bool  DOUBLE_BUFFER;
    int   INPUT_BUFFER; // for multiplayer
} VB_OPT;

void setCustomMappingDefaults(void);
void setDefaults(void);
int loadFileOptions(void);
int saveFileOptions(void);
int loadGameOptions(void);
int saveGameOptions(void);
int deleteGameOptions(void);

extern VB_OPT tVBOpt;
extern int vbkey[32];

#endif
