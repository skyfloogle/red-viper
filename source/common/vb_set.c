#include "cpp.h"
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef __3DS__
#include <3ds.h>
#endif

#include "inih/ini.h"
#include "vb_set.h"
#include "vb_dsp.h"
#include "vb_gui.h"

VB_OPT  tVBOpt;
int     vbkey[32] = {0};

#ifndef __3DS__
bool buttons_on_screen;
void setCustomControls() {}
void setPresetControls(bool buttons) {}
#endif

void setCustomMappingDefaults(void) {
    bool new_3ds = false;
    #ifdef __3DS__
    APT_CheckNew3DS(&new_3ds);
    #endif
    if (buttons_on_screen) {
        tVBOpt.CUSTOM_MAPPING_A = VB_RPAD_R;
        tVBOpt.CUSTOM_MAPPING_X = VB_RPAD_U;
        tVBOpt.CUSTOM_MAPPING_B = VB_RPAD_D;
        tVBOpt.CUSTOM_MAPPING_Y = VB_RPAD_L;
    } else {
        tVBOpt.CUSTOM_MAPPING_A = tVBOpt.ABXY_MODE < 2 || tVBOpt.ABXY_MODE == 4 ? VB_KEY_A : VB_KEY_B;
        tVBOpt.CUSTOM_MAPPING_Y = tVBOpt.ABXY_MODE < 2 || tVBOpt.ABXY_MODE == 5 ? VB_KEY_B : VB_KEY_A;
        tVBOpt.CUSTOM_MAPPING_B = tVBOpt.ABXY_MODE == 0 || tVBOpt.ABXY_MODE == 3 || tVBOpt.ABXY_MODE == 4 ? VB_KEY_B : VB_KEY_A;
        tVBOpt.CUSTOM_MAPPING_X = tVBOpt.ABXY_MODE == 0 || tVBOpt.ABXY_MODE == 3 || tVBOpt.ABXY_MODE == 5 ? VB_KEY_A : VB_KEY_B;
    }
    switch (tVBOpt.DPAD_MODE) {
        default: // VB LPAD
            tVBOpt.CUSTOM_MAPPING_DUP = VB_LPAD_U;
            tVBOpt.CUSTOM_MAPPING_DDOWN = VB_LPAD_D;
            tVBOpt.CUSTOM_MAPPING_DLEFT = VB_LPAD_L;
            tVBOpt.CUSTOM_MAPPING_DRIGHT = VB_LPAD_R;
            break;
        case 1: // VB RPAD
            tVBOpt.CUSTOM_MAPPING_DUP = VB_RPAD_U;
            tVBOpt.CUSTOM_MAPPING_DDOWN = VB_RPAD_D;
            tVBOpt.CUSTOM_MAPPING_DLEFT = VB_RPAD_L;
            tVBOpt.CUSTOM_MAPPING_DRIGHT = VB_RPAD_R;
            break;
        case 2: // Mirror ABXY buttons
            tVBOpt.CUSTOM_MAPPING_DUP = tVBOpt.CUSTOM_MAPPING_X;
            tVBOpt.CUSTOM_MAPPING_DDOWN = tVBOpt.CUSTOM_MAPPING_B;
            tVBOpt.CUSTOM_MAPPING_DLEFT = tVBOpt.CUSTOM_MAPPING_Y;
            tVBOpt.CUSTOM_MAPPING_DRIGHT = tVBOpt.CUSTOM_MAPPING_A;
            break;
    }
    if (new_3ds) {
        tVBOpt.CUSTOM_MAPPING_L = tVBOpt.ZLZR_MODE <= 1 ? VB_KEY_L : tVBOpt.ZLZR_MODE == 2 ? VB_KEY_B : VB_KEY_A;
        tVBOpt.CUSTOM_MAPPING_R = tVBOpt.ZLZR_MODE <= 1 ? VB_KEY_R : tVBOpt.ZLZR_MODE == 2 ? VB_KEY_A : VB_KEY_B;
        tVBOpt.CUSTOM_MAPPING_ZL = tVBOpt.ZLZR_MODE == 0 ? VB_KEY_B : tVBOpt.ZLZR_MODE == 1 ? VB_KEY_A : VB_KEY_L;
        tVBOpt.CUSTOM_MAPPING_ZR = tVBOpt.ZLZR_MODE == 0 ? VB_KEY_A : tVBOpt.ZLZR_MODE == 1 ? VB_KEY_B : VB_KEY_R;
    } else {
        tVBOpt.CUSTOM_MAPPING_L = VB_KEY_L;
        tVBOpt.CUSTOM_MAPPING_R = VB_KEY_R;
    }
    tVBOpt.CUSTOM_MAPPING_CPAD_UP      = VB_LPAD_U;
    tVBOpt.CUSTOM_MAPPING_CPAD_DOWN    = VB_LPAD_D;
    tVBOpt.CUSTOM_MAPPING_CPAD_LEFT    = VB_LPAD_L;
    tVBOpt.CUSTOM_MAPPING_CPAD_RIGHT   = VB_LPAD_R;
    tVBOpt.CUSTOM_MAPPING_CSTICK_UP    = VB_RPAD_U;
    tVBOpt.CUSTOM_MAPPING_CSTICK_DOWN  = VB_RPAD_D;
    tVBOpt.CUSTOM_MAPPING_CSTICK_LEFT  = VB_RPAD_L;
    tVBOpt.CUSTOM_MAPPING_CSTICK_RIGHT = VB_RPAD_R;
    tVBOpt.CUSTOM_MAPPING_START        = VB_KEY_START;
    tVBOpt.CUSTOM_MAPPING_SELECT       = VB_KEY_SELECT;

    memset(tVBOpt.CUSTOM_MOD, 0, sizeof(tVBOpt.CUSTOM_MOD));
}

void setDefaults(void) {
    // Set up the Defaults
    tVBOpt.MAXCYCLES = 400;
    tVBOpt.FRMSKIP  = 0;
    tVBOpt.DSPSWAP  = 0;
    tVBOpt.PALMODE  = PAL_NORMAL;
    tVBOpt.DEBUG    = DEBUGLEVEL != 0;
    tVBOpt.STDOUT   = 0;
    tVBOpt.BFACTOR  = 64;
    tVBOpt.SCR_X    = 400;
    tVBOpt.SCR_Y    = 240;
    tVBOpt.SCR_MODE = 0;
    tVBOpt.FIXPAL   = 0;
    tVBOpt.DISASM   = 0;
    tVBOpt.SOUND    = 1;
    tVBOpt.DSP2X    = 0;
    tVBOpt.FASTFORWARD = 0;
    tVBOpt.FF_TOGGLE = 0;
    tVBOpt.RENDERMODE = RM_TOGPU;
    tVBOpt.PAUSE_RIGHT = 160;
    tVBOpt.TOUCH_AX = 250;
    tVBOpt.TOUCH_AY = 64;
    tVBOpt.TOUCH_BX = 250;
    tVBOpt.TOUCH_BY = 160;
    tVBOpt.TOUCH_PADX = 240;
    tVBOpt.TOUCH_PADY = 128;
    tVBOpt.TOUCH_BUTTONS = 0;
    tVBOpt.TOUCH_SWITCH = true;
    tVBOpt.CPP_ENABLED = true;
    tVBOpt.ABXY_MODE = 0;
    tVBOpt.ZLZR_MODE = 0;
    tVBOpt.DPAD_MODE = 0;
    tVBOpt.CUSTOM_CONTROLS = 0;
    setCustomMappingDefaults();
    tVBOpt.MULTICOL = false;
    tVBOpt.TINT = 0x0000ff;
    tVBOpt.MULTIID = 0;
    tVBOpt.MTINT[0][0] = 0;
    tVBOpt.MTINT[0][1] = tVBOpt.MTINT[0][2] = tVBOpt.MTINT[0][3] = 0x0000ff;
    tVBOpt.STINT[0][0] = tVBOpt.STINT[0][1] = tVBOpt.STINT[0][2] = 1.0;
    tVBOpt.MTINT[1][0] = 0x394129;
    tVBOpt.MTINT[1][1] = tVBOpt.MTINT[1][2] = tVBOpt.MTINT[1][3] = 0x10827b;
    tVBOpt.STINT[1][0] = tVBOpt.STINT[1][1] = tVBOpt.STINT[1][2] = 1.0;
    tVBOpt.MTINT[2][0] = 0x000208;
    tVBOpt.MTINT[2][1] = 0x031342;
    tVBOpt.MTINT[2][2] = 0x00bb1d;
    tVBOpt.MTINT[2][3] = 0x00d8ff;
    tVBOpt.STINT[2][0] = 2.5;
    tVBOpt.STINT[2][1] = tVBOpt.STINT[2][2] = 1.0;
    tVBOpt.MTINT[3][0] = 0xffffff;
    tVBOpt.MTINT[3][1] = tVBOpt.MTINT[3][2] = tVBOpt.MTINT[3][3] = 0;
    tVBOpt.STINT[3][0] = tVBOpt.STINT[3][1] = tVBOpt.STINT[3][2] = 1.0;
    tVBOpt.SLIDERMODE = SLIDER_3DS;
    tVBOpt.DEFAULT_EYE = 0;
    tVBOpt.PERF_INFO = false;
    tVBOpt.ROM_PATH[0] = 0;
    tVBOpt.LAST_ROM[0] = 0;
    tVBOpt.VSYNC = true;
    tVBOpt.N3DS_SPEEDUP = true;
    tVBOpt.ANAGLYPH_LEFT = 0b001;
    tVBOpt.ANAGLYPH_RIGHT = 0b110;
    tVBOpt.ANAGLYPH_DEPTH = 0;
    tVBOpt.GAME_SETTINGS = false;
    tVBOpt.MODIFIED = false;
    tVBOpt.INPUTS = false;
    tVBOpt.ANTIFLICKER = false;
    tVBOpt.VIP_OVERCLOCK = false;
    tVBOpt.FORWARDER = false;
    tVBOpt.DOUBLE_BUFFER = false;
    tVBOpt.INPUT_BUFFER = 2;
    strcpy(tVBOpt.HOME_PATH, "sdmc:/red-viper");

    // Default keys
#ifdef __3DS__
    vbkey[__builtin_ctz(KEY_DUP)] = VB_LPAD_U;
    vbkey[__builtin_ctz(KEY_DDOWN)] = VB_LPAD_D;
    vbkey[__builtin_ctz(KEY_DLEFT)] = VB_LPAD_L;
    vbkey[__builtin_ctz(KEY_DRIGHT)] = VB_LPAD_R;

    vbkey[__builtin_ctz(KEY_CPAD_UP)] = VB_LPAD_U;
    vbkey[__builtin_ctz(KEY_CPAD_DOWN)] = VB_LPAD_D;
    vbkey[__builtin_ctz(KEY_CPAD_LEFT)] = VB_LPAD_L;
    vbkey[__builtin_ctz(KEY_CPAD_RIGHT)] = VB_LPAD_R;

    vbkey[__builtin_ctz(KEY_CSTICK_UP)] = VB_RPAD_U;
    vbkey[__builtin_ctz(KEY_CSTICK_DOWN)] = VB_RPAD_D;
    vbkey[__builtin_ctz(KEY_CSTICK_LEFT)] = VB_RPAD_L;
    vbkey[__builtin_ctz(KEY_CSTICK_RIGHT)] = VB_RPAD_R;

    vbkey[__builtin_ctz(KEY_A)] = VB_KEY_A;
    vbkey[__builtin_ctz(KEY_X)] = VB_KEY_A;
    vbkey[__builtin_ctz(KEY_B)] = VB_KEY_B;
    vbkey[__builtin_ctz(KEY_Y)] = VB_KEY_B;

    vbkey[__builtin_ctz(KEY_START)] = VB_KEY_START;
    vbkey[__builtin_ctz(KEY_SELECT)] = VB_KEY_SELECT;

    vbkey[__builtin_ctz(KEY_L)] = VB_KEY_L;
    vbkey[__builtin_ctz(KEY_R)] = VB_KEY_R;
    vbkey[__builtin_ctz(KEY_ZL)] = VB_KEY_B;
    vbkey[__builtin_ctz(KEY_ZR)] = VB_KEY_A;

    toggleAnaglyph(false);
#endif
}

// inih handler
static int handler(void* user, const char* section, const char* name,
                   const char* value) {
    VB_OPT* pconfig = (VB_OPT*)user;

    #define MATCH(s, n) (strcmp(section, s) == 0 && strcmp(name, n) == 0)
    if (MATCH("vbopt", "multicol")) {
        pconfig->MULTICOL = atoi(value);
    } else if (MATCH("vbopt", "tint")) {
        pconfig->TINT = atoi(value);
    } else if (MATCH("vbopt", "multicolid")) {
        pconfig->MULTIID = atoi(value);

    // multitint - legacy
    } else if (MATCH("vbopt", "multitint0")) {
        pconfig->MTINT[0][0] = atoi(value);
    } else if (MATCH("vbopt", "multitint1")) {
        pconfig->MTINT[0][1] = atoi(value);
    } else if (MATCH("vbopt", "multitint2")) {
        pconfig->MTINT[0][2] = atoi(value);
    } else if (MATCH("vbopt", "multitint3")) {
        pconfig->MTINT[0][3] = atoi(value);
    } else if (MATCH("vbopt", "tintscale1")) {
        pconfig->STINT[0][0] = atof(value);
    } else if (MATCH("vbopt", "tintscale2")) {
        pconfig->STINT[0][1] = atof(value);
    } else if (MATCH("vbopt", "tintscale3")) {
        pconfig->STINT[0][2] = atof(value);

    } else if (MATCH("vbopt", "slidermode")) {
        pconfig->SLIDERMODE = atoi(value);
    } else if (MATCH("vbopt", "default_eye")) {
        pconfig->DEFAULT_EYE = atoi(value);
    } else if (MATCH("vbopt", "antiflicker")) {
        pconfig->ANTIFLICKER = atoi(value);
    } else if (MATCH("vbopt", "perfinfo")) {
        pconfig->PERF_INFO = atoi(value);
    } else if (MATCH("vbopt", "lastrom")) {
        strcpy(pconfig->ROM_PATH, value);
        strcpy(pconfig->LAST_ROM, value);
    } else if (MATCH("vbopt", "n3ds_speedup")) {
        pconfig->N3DS_SPEEDUP = atoi(value);
    } else if (MATCH("vbopt", "vip_overclock")) {
        pconfig->VIP_OVERCLOCK = atoi(value);
    } else if (MATCH("vbopt", "homepath")) {
        strncpy(pconfig->HOME_PATH, value, sizeof(pconfig->HOME_PATH));
        pconfig->HOME_PATH[sizeof(pconfig->HOME_PATH)-1] = 0;
        // remove trailing slash
        char *last_slash = strrchr(pconfig->HOME_PATH, '/');
        if (last_slash != NULL && last_slash[1] == 0)
            *last_slash = 0;
    } else if (MATCH("vbopt", "cpp_enabled")) {
        pconfig->CPP_ENABLED = atoi(value);
    } else if (MATCH("vbopt", "custom_controls")) {
        pconfig->CUSTOM_CONTROLS = atoi(value);
    } else if (MATCH("vbopt", "abxy") || MATCH("controls_preset", "abxy")) {
        pconfig->ABXY_MODE = atoi(value) % 6;
    } else if (MATCH("vbopt", "zlzr") || MATCH("controls_preset", "zlzr")) {
        pconfig->ZLZR_MODE = atoi(value) % 4;
    } else if (MATCH("vbopt", "dpad_mode") || MATCH("controls_preset", "dpad")) {
        pconfig->DPAD_MODE = atoi(value) % 3;
    } else if (MATCH("anaglyph", "enabled")) {
        #ifdef __3DS__
        toggleAnaglyph(atoi(value));
        #endif
    } else if (MATCH("anaglyph", "left")) {
        tVBOpt.ANAGLYPH_LEFT = atoi(value);
    } else if (MATCH("anaglyph", "right")) {
        tVBOpt.ANAGLYPH_RIGHT = atoi(value);
    } else if (MATCH("anaglyph", "depth")) {
        tVBOpt.ANAGLYPH_DEPTH = atoi(value);
    } else if (MATCH("controls_custom", "dup")) {
        pconfig->CUSTOM_MAPPING_DUP = atoi(value);
    } else if (MATCH("controls_custom", "ddown")) {
        pconfig->CUSTOM_MAPPING_DDOWN = atoi(value);
    } else if (MATCH("controls_custom", "dleft")) {
        pconfig->CUSTOM_MAPPING_DLEFT = atoi(value);
    } else if (MATCH("controls_custom", "dright")) {
        pconfig->CUSTOM_MAPPING_DRIGHT = atoi(value);
    } else if (MATCH("controls_custom", "cpad_up")) {
        pconfig->CUSTOM_MAPPING_CPAD_UP = atoi(value);
    } else if (MATCH("controls_custom", "cpad_down")) {
        pconfig->CUSTOM_MAPPING_CPAD_DOWN = atoi(value);
    } else if (MATCH("controls_custom", "cpad_left")) {
        pconfig->CUSTOM_MAPPING_CPAD_LEFT = atoi(value);
    } else if (MATCH("controls_custom", "cpad_right")) {
        pconfig->CUSTOM_MAPPING_CPAD_RIGHT = atoi(value);
    } else if (MATCH("controls_custom", "cstick_up")) {
        pconfig->CUSTOM_MAPPING_CSTICK_UP = atoi(value);
    } else if (MATCH("controls_custom", "cstick_down")) {
        pconfig->CUSTOM_MAPPING_CSTICK_DOWN = atoi(value);
    } else if (MATCH("controls_custom", "cstick_left")) {
        pconfig->CUSTOM_MAPPING_CSTICK_LEFT = atoi(value);
    } else if (MATCH("controls_custom", "cstick_right")) {
        pconfig->CUSTOM_MAPPING_CSTICK_RIGHT = atoi(value);
    } else if (MATCH("controls_custom", "a")) {
        pconfig->CUSTOM_MAPPING_A = atoi(value);
    } else if (MATCH("controls_custom", "x")) {
        pconfig->CUSTOM_MAPPING_X = atoi(value);
    } else if (MATCH("controls_custom", "b")) {
        pconfig->CUSTOM_MAPPING_B = atoi(value);
    } else if (MATCH("controls_custom", "y")) {
        pconfig->CUSTOM_MAPPING_Y = atoi(value);
    } else if (MATCH("controls_custom", "start")) {
        pconfig->CUSTOM_MAPPING_START = atoi(value);
    } else if (MATCH("controls_custom", "select")) {
        pconfig->CUSTOM_MAPPING_SELECT = atoi(value);
    } else if (MATCH("controls_custom", "l")) {
        pconfig->CUSTOM_MAPPING_L = atoi(value);
    } else if (MATCH("controls_custom", "r")) {
        pconfig->CUSTOM_MAPPING_R = atoi(value);
    } else if (MATCH("controls_custom", "zl")) {
        pconfig->CUSTOM_MAPPING_ZL = atoi(value);
    } else if (MATCH("controls_custom", "zr")) {
        pconfig->CUSTOM_MAPPING_ZR = atoi(value);
    #ifdef __3DS__
    } else if (MATCH("controls_mod", "dup")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_DUP)] = atoi(value);
    } else if (MATCH("controls_mod", "ddown")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_DDOWN)] = atoi(value);
    } else if (MATCH("controls_mod", "dleft")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_DLEFT)] = atoi(value);
    } else if (MATCH("controls_mod", "dright")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_DRIGHT)] = atoi(value);
    } else if (MATCH("controls_mod", "cpad_up")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_CPAD_UP)] = atoi(value);
    } else if (MATCH("controls_mod", "cpad_down")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_CPAD_DOWN)] = atoi(value);
    } else if (MATCH("controls_mod", "cpad_left")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_CPAD_LEFT)] = atoi(value);
    } else if (MATCH("controls_mod", "cpad_right")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_CPAD_RIGHT)] = atoi(value);
    } else if (MATCH("controls_mod", "cstick_up")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_CSTICK_UP)] = atoi(value);
    } else if (MATCH("controls_mod", "cstick_down")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_CSTICK_DOWN)] = atoi(value);
    } else if (MATCH("controls_mod", "cstick_left")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_CSTICK_LEFT)] = atoi(value);
    } else if (MATCH("controls_mod", "cstick_right")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_CSTICK_RIGHT)] = atoi(value);
    } else if (MATCH("controls_mod", "a")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_A)] = atoi(value);
    } else if (MATCH("controls_mod", "x")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_X)] = atoi(value);
    } else if (MATCH("controls_mod", "b")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_B)] = atoi(value);
    } else if (MATCH("controls_mod", "y")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_Y)] = atoi(value);
    } else if (MATCH("controls_mod", "start")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_START)] = atoi(value);
    } else if (MATCH("controls_mod", "select")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_SELECT)] = atoi(value);
    } else if (MATCH("controls_mod", "l")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_L)] = atoi(value);
    } else if (MATCH("controls_mod", "r")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_R)] = atoi(value);
    } else if (MATCH("controls_mod", "zl")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_ZL)] = atoi(value);
    } else if (MATCH("controls_mod", "zr")) {
        pconfig->CUSTOM_MOD[__builtin_ctz(KEY_ZR)] = atoi(value);
    #endif
    } else if (MATCH("touch", "ax")) {
        tVBOpt.TOUCH_AX = atoi(value);
    } else if (MATCH("touch", "ay")) {
        tVBOpt.TOUCH_AY = atoi(value);
    } else if (MATCH("touch", "bx")) {
        tVBOpt.TOUCH_BX = atoi(value);
    } else if (MATCH("touch", "by")) {
        tVBOpt.TOUCH_BY = atoi(value);
    } else if (MATCH("touch", "padx")) {
        tVBOpt.TOUCH_PADX = atoi(value);
    } else if (MATCH("touch", "pady")) {
        tVBOpt.TOUCH_PADY = atoi(value);
    } else if (MATCH("touch", "pausex")) {
        tVBOpt.PAUSE_RIGHT = atoi(value);
    } else if (MATCH("touch", "ff_mode")) {
        tVBOpt.FF_TOGGLE = atoi(value);
    } else if (MATCH("touch", "buttons")) {
        tVBOpt.TOUCH_BUTTONS = atoi(value);
    } else if (MATCH("touch", "switch")) {
        tVBOpt.TOUCH_SWITCH = atoi(value);
    } else if (MATCH("touch", "inputs")) {
        tVBOpt.INPUTS = atoi(value);

    } else if (MATCH("palette0", "tint0")) {
        pconfig->MTINT[0][0] = atoi(value);
    } else if (MATCH("palette0", "tint1")) {
        pconfig->MTINT[0][1] = atoi(value);
    } else if (MATCH("palette0", "tint2")) {
        pconfig->MTINT[0][2] = atoi(value);
    } else if (MATCH("palette0", "tint3")) {
        pconfig->MTINT[0][3] = atoi(value);
    } else if (MATCH("palette0", "scale1")) {
        pconfig->STINT[0][0] = atof(value);
    } else if (MATCH("palette0", "scale2")) {
        pconfig->STINT[0][1] = atof(value);
    } else if (MATCH("palette0", "scale3")) {
        pconfig->STINT[0][2] = atof(value);

    } else if (MATCH("palette1", "tint0")) {
        pconfig->MTINT[1][0] = atoi(value);
    } else if (MATCH("palette1", "tint1")) {
        pconfig->MTINT[1][1] = atoi(value);
    } else if (MATCH("palette1", "tint2")) {
        pconfig->MTINT[1][2] = atoi(value);
    } else if (MATCH("palette1", "tint3")) {
        pconfig->MTINT[1][3] = atoi(value);
    } else if (MATCH("palette1", "scale1")) {
        pconfig->STINT[1][0] = atof(value);
    } else if (MATCH("palette1", "scale2")) {
        pconfig->STINT[1][1] = atof(value);
    } else if (MATCH("palette1", "scale3")) {
        pconfig->STINT[1][2] = atof(value);

    } else if (MATCH("palette2", "tint0")) {
        pconfig->MTINT[2][0] = atoi(value);
    } else if (MATCH("palette2", "tint1")) {
        pconfig->MTINT[2][1] = atoi(value);
    } else if (MATCH("palette2", "tint2")) {
        pconfig->MTINT[2][2] = atoi(value);
    } else if (MATCH("palette2", "tint3")) {
        pconfig->MTINT[2][3] = atoi(value);
    } else if (MATCH("palette2", "scale1")) {
        pconfig->STINT[2][0] = atof(value);
    } else if (MATCH("palette2", "scale2")) {
        pconfig->STINT[2][1] = atof(value);
    } else if (MATCH("palette2", "scale3")) {
        pconfig->STINT[2][2] = atof(value);

    } else if (MATCH("palette3", "tint0")) {
        pconfig->MTINT[3][0] = atoi(value);
    } else if (MATCH("palette3", "tint1")) {
        pconfig->MTINT[3][1] = atoi(value);
    } else if (MATCH("palette3", "tint2")) {
        pconfig->MTINT[3][2] = atoi(value);
    } else if (MATCH("palette3", "tint3")) {
        pconfig->MTINT[3][3] = atoi(value);
    } else if (MATCH("palette3", "scale1")) {
        pconfig->STINT[3][0] = atof(value);
    } else if (MATCH("palette3", "scale2")) {
        pconfig->STINT[3][1] = atof(value);
    } else if (MATCH("palette3", "scale3")) {
        pconfig->STINT[3][2] = atof(value);
        
    } else {
        return 1;  // unknown section/name, ignore
    }
    return 1;
}

static char *getGameIniPath(void) {
    struct stat st;
    char *last_slash = strrchr(tVBOpt.ROM_PATH, '/');
    if (last_slash == NULL) return NULL;
    static char inipath[300];
    // $HOME/configs
    snprintf(inipath, sizeof(inipath), "%s/configs", tVBOpt.HOME_PATH);
    if (stat(tVBOpt.HOME_PATH, &st) == -1) {
        if (mkdir(tVBOpt.HOME_PATH, 0777)) return NULL;
    }
    if (stat(inipath, &st) == -1) {
        if (mkdir(inipath, 0777)) return NULL;
    }
    // $HOME/configs/game.ini
    strncat(inipath, last_slash, sizeof(inipath) - strlen(inipath) - 1);
    char *end = strrchr(inipath, '.');
    if (!end) end = inipath + strlen(inipath);
    // vague bounds check
    if (end - inipath >= 290) return NULL;
    strcpy(end, ".ini");
    return inipath;
}

int loadFileOptions(void) {
    struct stat st;
    if (stat(CONFIG_FILENAME, &st) == -1 && stat(CONFIG_FILENAME_LEGACY, &st) != -1) {
        if (stat("sdmc:/config", &st) == -1) mkdir("sdmc:/config", 0777);
        if (stat("sdmc:/config/red-viper", &st) == -1) mkdir("sdmc:/config/red-viper", 0777);
        rename(CONFIG_FILENAME_LEGACY, CONFIG_FILENAME);
    }

    bool old_cpp = tVBOpt.CPP_ENABLED;

    int ret = ini_parse(CONFIG_FILENAME, handler, &tVBOpt);
    if (!ret) tVBOpt.GAME_SETTINGS = false;
    tVBOpt.MODIFIED = false;
    buttons_on_screen = tVBOpt.TOUCH_BUTTONS;
    tVBOpt.CUSTOM_CONTROLS ? setCustomControls() : setPresetControls(buttons_on_screen);

    #ifdef __3DS__
    osSetSpeedupEnable(tVBOpt.N3DS_SPEEDUP);
    if (tVBOpt.CPP_ENABLED != old_cpp) {
        if (tVBOpt.CPP_ENABLED) cppInit();
        else cppExit();
    }
    #endif

    return ret;
}

int loadGameOptions(void) {
    bool old_cpp = tVBOpt.CPP_ENABLED;

    char *ini_path = getGameIniPath();
    int ret = ENOENT;
    if (ini_path) ret = ini_parse(ini_path, handler, &tVBOpt);
    if (!ret) tVBOpt.GAME_SETTINGS = true;
    else tVBOpt.GAME_SETTINGS = false;
    tVBOpt.MODIFIED = false;
    buttons_on_screen = tVBOpt.TOUCH_BUTTONS;
    tVBOpt.CUSTOM_CONTROLS ? setCustomControls() : setPresetControls(buttons_on_screen);
    #ifdef __3DS__
    osSetSpeedupEnable(tVBOpt.N3DS_SPEEDUP);
    if (tVBOpt.CPP_ENABLED != old_cpp) {
        if (tVBOpt.CPP_ENABLED) cppInit();
        else cppExit();
    }
    #endif
    
    return ret;
}

void writeOptionsFile(FILE* f, bool global) {
    fprintf(f, "[vbopt]\n");
    fprintf(f, "multicol=%d\n", tVBOpt.MULTICOL);
    fprintf(f, "tint=%d\n", tVBOpt.TINT);
    fprintf(f, "multicolid=%d\n", tVBOpt.MULTIID);
    fprintf(f, "slidermode=%d\n", tVBOpt.SLIDERMODE);
    fprintf(f, "default_eye=%d\n", tVBOpt.DEFAULT_EYE);
    fprintf(f, "antiflicker=%d\n", tVBOpt.ANTIFLICKER);
    fprintf(f, "perfinfo=%d\n", tVBOpt.PERF_INFO);
    fprintf(f, "n3ds_speedup=%d\n", tVBOpt.N3DS_SPEEDUP);
    fprintf(f, "vip_overclock=%d\n", tVBOpt.VIP_OVERCLOCK);
    if (global) {
        fprintf(f, "lastrom=%s\n", tVBOpt.LAST_ROM);
        fprintf(f, "homepath=%s\n", tVBOpt.HOME_PATH);
    }
    fprintf(f, "cpp_enabled=%d\n", tVBOpt.CPP_ENABLED);
    fprintf(f, "custom_controls=%d\n", tVBOpt.CUSTOM_CONTROLS);
    fprintf(f, "\n[controls_preset]\n");
    fprintf(f, "abxy=%d\n", tVBOpt.ABXY_MODE);
    fprintf(f, "zlzr=%d\n", tVBOpt.ZLZR_MODE);
    fprintf(f, "dpad=%d\n", tVBOpt.DPAD_MODE);
    fprintf(f, "\n[anaglyph]\n");
    fprintf(f, "enabled=%d\n", tVBOpt.ANAGLYPH);
    fprintf(f, "left=%d\n", tVBOpt.ANAGLYPH_LEFT);
    fprintf(f, "right=%d\n", tVBOpt.ANAGLYPH_RIGHT);
    fprintf(f, "depth=%d\n", tVBOpt.ANAGLYPH_DEPTH);
    fprintf(f, "\n[controls_custom]\n");
    fprintf(f, "dup=%d\n", tVBOpt.CUSTOM_MAPPING_DUP);
    fprintf(f, "ddown=%d\n", tVBOpt.CUSTOM_MAPPING_DDOWN);
    fprintf(f, "dleft=%d\n", tVBOpt.CUSTOM_MAPPING_DLEFT);
    fprintf(f, "dright=%d\n", tVBOpt.CUSTOM_MAPPING_DRIGHT);
    fprintf(f, "cpad_up=%d\n", tVBOpt.CUSTOM_MAPPING_CPAD_UP);
    fprintf(f, "cpad_down=%d\n", tVBOpt.CUSTOM_MAPPING_CPAD_DOWN);
    fprintf(f, "cpad_left=%d\n", tVBOpt.CUSTOM_MAPPING_CPAD_LEFT);
    fprintf(f, "cpad_right=%d\n", tVBOpt.CUSTOM_MAPPING_CPAD_RIGHT);
    fprintf(f, "cstick_up=%d\n", tVBOpt.CUSTOM_MAPPING_CSTICK_UP);
    fprintf(f, "cstick_down=%d\n", tVBOpt.CUSTOM_MAPPING_CSTICK_DOWN);
    fprintf(f, "cstick_left=%d\n", tVBOpt.CUSTOM_MAPPING_CSTICK_LEFT);
    fprintf(f, "cstick_right=%d\n", tVBOpt.CUSTOM_MAPPING_CSTICK_RIGHT);
    fprintf(f, "a=%d\n", tVBOpt.CUSTOM_MAPPING_A);
    fprintf(f, "x=%d\n", tVBOpt.CUSTOM_MAPPING_X);
    fprintf(f, "b=%d\n", tVBOpt.CUSTOM_MAPPING_B);
    fprintf(f, "y=%d\n", tVBOpt.CUSTOM_MAPPING_Y);
    fprintf(f, "start=%d\n", tVBOpt.CUSTOM_MAPPING_START);
    fprintf(f, "select=%d\n", tVBOpt.CUSTOM_MAPPING_SELECT);
    fprintf(f, "l=%d\n", tVBOpt.CUSTOM_MAPPING_L);
    fprintf(f, "r=%d\n", tVBOpt.CUSTOM_MAPPING_R);
    fprintf(f, "zl=%d\n", tVBOpt.CUSTOM_MAPPING_ZL);
    fprintf(f, "zr=%d\n", tVBOpt.CUSTOM_MAPPING_ZR);
    fprintf(f, "\n[controls_mod]\n");
    #ifdef __3DS__
    fprintf(f, "dup=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_DUP)]);
    fprintf(f, "ddown=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_DDOWN)]);
    fprintf(f, "dleft=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_DLEFT)]);
    fprintf(f, "dright=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_DRIGHT)]);
    fprintf(f, "cpad_up=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_CPAD_UP)]);
    fprintf(f, "cpad_down=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_CPAD_DOWN)]);
    fprintf(f, "cpad_left=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_CPAD_LEFT)]);
    fprintf(f, "cpad_right=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_CPAD_RIGHT)]);
    fprintf(f, "cstick_up=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_CSTICK_UP)]);
    fprintf(f, "cstick_down=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_CSTICK_DOWN)]);
    fprintf(f, "cstick_left=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_CSTICK_LEFT)]);
    fprintf(f, "cstick_right=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_CSTICK_RIGHT)]);
    fprintf(f, "a=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_A)]);
    fprintf(f, "x=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_X)]);
    fprintf(f, "b=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_B)]);
    fprintf(f, "y=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_Y)]);
    fprintf(f, "start=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_START)]);
    fprintf(f, "select=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_SELECT)]);
    fprintf(f, "l=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_L)]);
    fprintf(f, "r=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_R)]);
    fprintf(f, "zl=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_ZL)]);
    fprintf(f, "zr=%d\n", tVBOpt.CUSTOM_MOD[__builtin_ctz(KEY_ZR)]);
    #endif
    fprintf(f, "\n[touch]\n");
    fprintf(f, "ax=%d\n", tVBOpt.TOUCH_AX);
    fprintf(f, "ay=%d\n", tVBOpt.TOUCH_AY);
    fprintf(f, "bx=%d\n", tVBOpt.TOUCH_BX);
    fprintf(f, "by=%d\n", tVBOpt.TOUCH_BY);
    fprintf(f, "padx=%d\n", tVBOpt.TOUCH_PADX);
    fprintf(f, "pady=%d\n", tVBOpt.TOUCH_PADY);
    fprintf(f, "pausex=%d\n", tVBOpt.PAUSE_RIGHT);
    fprintf(f, "ff_mode=%d\n", tVBOpt.FF_TOGGLE);
    fprintf(f, "buttons=%d\n", tVBOpt.TOUCH_BUTTONS);
    fprintf(f, "switch=%d\n", tVBOpt.TOUCH_SWITCH);
    fprintf(f, "inputs=%d\n", tVBOpt.INPUTS);
    for (int i = 0; i < 4; i++) {
        fprintf(f, "\n[palette%d]\n", i);
    fprintf(f, "tint0=%d\n", tVBOpt.MTINT[i][0]);
    fprintf(f, "tint1=%d\n", tVBOpt.MTINT[i][1]);
    fprintf(f, "tint2=%d\n", tVBOpt.MTINT[i][2]);
    fprintf(f, "tint3=%d\n", tVBOpt.MTINT[i][3]);
    fprintf(f, "scale1=%f\n", tVBOpt.STINT[i][0]);
    fprintf(f, "scale2=%f\n", tVBOpt.STINT[i][1]);
    fprintf(f, "scale3=%f\n", tVBOpt.STINT[i][2]);
    }
}

int saveFileOptions(void) {
    if (!tVBOpt.FORWARDER) strcpy(tVBOpt.LAST_ROM, tVBOpt.ROM_PATH);

    struct stat st;
    if (stat("sdmc:/config", &st) == -1) mkdir("sdmc:/config", 0777);
    if (stat("sdmc:/config/red-viper", &st) == -1) mkdir("sdmc:/config/red-viper", 0777);
    FILE* f = fopen(CONFIG_FILENAME, "w");
    if (!f)
        return 1;

    writeOptionsFile(f, true);

    fclose(f);
    tVBOpt.GAME_SETTINGS = false;
    tVBOpt.MODIFIED = false;
    return 0;
}

int deleteGameOptions(void) {
    char *ini_path = getGameIniPath();
    if (ini_path) return remove(getGameIniPath());
    else return ENOENT;
}

int saveGameOptions(void) {
    char *ini_path = getGameIniPath();
    if (!ini_path) return 1;
    FILE* f = fopen(ini_path, "w");
    if (!f)
        return 1;

    writeOptionsFile(f, false);

    fclose(f);
    tVBOpt.GAME_SETTINGS = true;
    tVBOpt.MODIFIED = false;
    return 0;
}
