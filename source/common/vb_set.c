#include <string.h>
#include <stdlib.h>

#ifdef __3DS__
#include <3ds.h>
#endif

#include "inih/ini.h"
#include "vb_types.h"
#include "vb_set.h"
#include "vb_dsp.h"

VB_OPT  tVBOpt;
int     vbkey[32] = {0};

void setCustomMappingDefaults(void) {
    bool new_3ds = false;
    APT_CheckNew3DS(&new_3ds);
    if (new_3ds) { // Since the new 3ds has a c-stick, both the Circle pad and DPad can map to the VB's L-DPad by default.
        tVBOpt.CUSTOM_MAPPING_DUP      = VB_LPAD_U;
        tVBOpt.CUSTOM_MAPPING_DDOWN    = VB_LPAD_D;
        tVBOpt.CUSTOM_MAPPING_DLEFT    = VB_LPAD_L;
        tVBOpt.CUSTOM_MAPPING_DRIGHT   = VB_LPAD_R;
    } else { // Since the old 3ds doesn't have a c-stick, it should have a hardware mapping to the VB's R-DPad by default.
        tVBOpt.CUSTOM_MAPPING_DUP      = VB_RPAD_U;
        tVBOpt.CUSTOM_MAPPING_DDOWN    = VB_RPAD_D;
        tVBOpt.CUSTOM_MAPPING_DLEFT    = VB_RPAD_L;
        tVBOpt.CUSTOM_MAPPING_DRIGHT   = VB_RPAD_R;
    }
    tVBOpt.CUSTOM_MAPPING_CPAD_UP      = VB_LPAD_U;
    tVBOpt.CUSTOM_MAPPING_CPAD_DOWN    = VB_LPAD_D;
    tVBOpt.CUSTOM_MAPPING_CPAD_LEFT    = VB_LPAD_L;
    tVBOpt.CUSTOM_MAPPING_CPAD_RIGHT   = VB_LPAD_R;
    tVBOpt.CUSTOM_MAPPING_CSTICK_UP    = VB_RPAD_U;
    tVBOpt.CUSTOM_MAPPING_CSTICK_DOWN  = VB_RPAD_D;
    tVBOpt.CUSTOM_MAPPING_CSTICK_LEFT  = VB_RPAD_L;
    tVBOpt.CUSTOM_MAPPING_CSTICK_RIGHT = VB_RPAD_R;
    tVBOpt.CUSTOM_MAPPING_A            = VB_KEY_A;
    tVBOpt.CUSTOM_MAPPING_X            = VB_KEY_A;
    tVBOpt.CUSTOM_MAPPING_B            = VB_KEY_B;
    tVBOpt.CUSTOM_MAPPING_Y            = VB_KEY_B;
    tVBOpt.CUSTOM_MAPPING_START        = VB_KEY_START;
    tVBOpt.CUSTOM_MAPPING_SELECT       = VB_KEY_SELECT;
    tVBOpt.CUSTOM_MAPPING_L            = VB_KEY_L;
    tVBOpt.CUSTOM_MAPPING_R            = VB_KEY_R;
    tVBOpt.CUSTOM_MAPPING_ZL           = VB_KEY_B;
    tVBOpt.CUSTOM_MAPPING_ZR           = VB_KEY_A;
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
    tVBOpt.DYNAREC  = 1;
    tVBOpt.FASTFORWARD = 0;
    tVBOpt.FF_TOGGLE = 0;
    tVBOpt.RENDERMODE = 1;
    tVBOpt.PAUSE_RIGHT = 160;
    tVBOpt.TOUCH_AX = 250;
    tVBOpt.TOUCH_AY = 64;
    tVBOpt.TOUCH_BX = 250;
    tVBOpt.TOUCH_BY = 160;
    tVBOpt.TOUCH_PADX = 240;
    tVBOpt.TOUCH_PADY = 128;
    tVBOpt.ABXY_MODE = 0;
    tVBOpt.ZLZR_MODE = 0;
    tVBOpt.DPAD_MODE = 0;
    tVBOpt.CUSTOM_CONTROLS = 0;
    setCustomMappingDefaults();
    tVBOpt.TINT = 0xff0000ff;
    tVBOpt.SLIDERMODE = SLIDER_3DS;
    tVBOpt.DEFAULT_EYE = 0;
    tVBOpt.PERF_INFO = false;
    tVBOpt.ROM_PATH[0] = 0;
    tVBOpt.VSYNC = true;
    tVBOpt.N3DS_SPEEDUP = true;
    tVBOpt.ANAGLYPH = false;
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
#endif
}

// inih handler
static int handler(void* user, const char* section, const char* name,
                   const char* value) {
    VB_OPT* pconfig = (VB_OPT*)user;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("vbopt", "tint")) {
        pconfig->TINT = atoi(value);
    } else if (MATCH("vbopt", "slidermode")) {
        pconfig->SLIDERMODE = atoi(value);
    } else if (MATCH("vbopt", "default_eye")) {
        pconfig->DEFAULT_EYE = atoi(value);
    } else if (MATCH("vbopt", "perfinfo")) {
        pconfig->PERF_INFO = atoi(value);
    } else if (MATCH("vbopt", "lastrom")) {
        strcpy(pconfig->ROM_PATH, value);
    } else if (MATCH("vbopt", "abxy")) {
        pconfig->ABXY_MODE = atoi(value) % 6;
    } else if (MATCH("vbopt", "zlzr")) {
        pconfig->ZLZR_MODE = atoi(value) % 4;
    } else if (MATCH("vbopt", "dpad_mode")) {
        pconfig->DPAD_MODE = atoi(value) % 3;
    } else if (MATCH("vbopt", "custom_controls")) {
        pconfig->CUSTOM_CONTROLS = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_dup")) {
        pconfig->CUSTOM_MAPPING_DUP = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_ddown")) {
        pconfig->CUSTOM_MAPPING_DDOWN = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_dleft")) {
        pconfig->CUSTOM_MAPPING_DLEFT = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_dright")) {
        pconfig->CUSTOM_MAPPING_DRIGHT = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_cpad_up")) {
        pconfig->CUSTOM_MAPPING_CPAD_UP = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_cpad_down")) {
        pconfig->CUSTOM_MAPPING_CPAD_DOWN = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_cpad_left")) {
        pconfig->CUSTOM_MAPPING_CPAD_LEFT = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_cpad_right")) {
        pconfig->CUSTOM_MAPPING_CPAD_RIGHT = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_cstick_up")) {
        pconfig->CUSTOM_MAPPING_CSTICK_UP = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_cstick_down")) {
        pconfig->CUSTOM_MAPPING_CSTICK_DOWN = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_cstick_left")) {
        pconfig->CUSTOM_MAPPING_CSTICK_LEFT = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_cstick_right")) {
        pconfig->CUSTOM_MAPPING_CSTICK_RIGHT = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_a")) {
        pconfig->CUSTOM_MAPPING_A = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_x")) {
        pconfig->CUSTOM_MAPPING_X = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_b")) {
        pconfig->CUSTOM_MAPPING_B = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_y")) {
        pconfig->CUSTOM_MAPPING_Y = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_start")) {
        pconfig->CUSTOM_MAPPING_START = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_select")) {
        pconfig->CUSTOM_MAPPING_SELECT = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_l")) {
        pconfig->CUSTOM_MAPPING_L = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_r")) {
        pconfig->CUSTOM_MAPPING_R = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_zl")) {
        pconfig->CUSTOM_MAPPING_ZL = atoi(value);
    } else if (MATCH("vbopt", "custom_mapping_zr")) {
        pconfig->CUSTOM_MAPPING_ZR = atoi(value);
    } else if (MATCH("vbopt", "n3ds_speedup")) {
        pconfig->N3DS_SPEEDUP = atoi(value);
    } else if (MATCH("vbopt", "homepath")) {
        strncpy(pconfig->HOME_PATH, value, sizeof(pconfig->HOME_PATH));
        pconfig->HOME_PATH[sizeof(pconfig->HOME_PATH)-1] = 0;
        // remove trailing slash
        char *last_slash = strrchr(pconfig->HOME_PATH, '/');
        if (last_slash != NULL && last_slash[1] == 0)
            *last_slash = 0;
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
    } else {
        return 0;  // unknown section/name, error
    }
    return 1;
}

int loadFileOptions(void) {
    return ini_parse(CONFIG_FILENAME, handler, &tVBOpt);
}

int saveFileOptions(void) {
    FILE* f = fopen(CONFIG_FILENAME, "w");
    if (!f)
        return 1;

    fprintf(f, "[vbopt]\n");
    fprintf(f, "tint=%d\n", tVBOpt.TINT);
    fprintf(f, "slidermode=%d\n", tVBOpt.SLIDERMODE);
    fprintf(f, "default_eye=%d\n", tVBOpt.DEFAULT_EYE);
    fprintf(f, "perfinfo=%d\n", tVBOpt.PERF_INFO);
    fprintf(f, "lastrom=%s\n", tVBOpt.ROM_PATH);
    fprintf(f, "abxy=%d\n", tVBOpt.ABXY_MODE);
    fprintf(f, "zlzr=%d\n", tVBOpt.ZLZR_MODE);
    fprintf(f, "dpad_mode=%d\n", tVBOpt.DPAD_MODE);
    fprintf(f, "custom_controls=%d\n", tVBOpt.CUSTOM_CONTROLS);
    fprintf(f, "custom_mapping_dup=%d\n", tVBOpt.CUSTOM_MAPPING_DUP);
    fprintf(f, "custom_mapping_ddown=%d\n", tVBOpt.CUSTOM_MAPPING_DDOWN);
    fprintf(f, "custom_mapping_dleft=%d\n", tVBOpt.CUSTOM_MAPPING_DLEFT);
    fprintf(f, "custom_mapping_dright=%d\n", tVBOpt.CUSTOM_MAPPING_DRIGHT);
    fprintf(f, "custom_mapping_cpad_up=%d\n", tVBOpt.CUSTOM_MAPPING_CPAD_UP);
    fprintf(f, "custom_mapping_cpad_down=%d\n", tVBOpt.CUSTOM_MAPPING_CPAD_DOWN);
    fprintf(f, "custom_mapping_cpad_left=%d\n", tVBOpt.CUSTOM_MAPPING_CPAD_LEFT);
    fprintf(f, "custom_mapping_cpad_right=%d\n", tVBOpt.CUSTOM_MAPPING_CPAD_RIGHT);
    fprintf(f, "custom_mapping_cstick_up=%d\n", tVBOpt.CUSTOM_MAPPING_CSTICK_UP);
    fprintf(f, "custom_mapping_cstick_down=%d\n", tVBOpt.CUSTOM_MAPPING_CSTICK_DOWN);
    fprintf(f, "custom_mapping_cstick_left=%d\n", tVBOpt.CUSTOM_MAPPING_CSTICK_LEFT);
    fprintf(f, "custom_mapping_cstick_right=%d\n", tVBOpt.CUSTOM_MAPPING_CSTICK_RIGHT);
    fprintf(f, "custom_mapping_a=%d\n", tVBOpt.CUSTOM_MAPPING_A);
    fprintf(f, "custom_mapping_x=%d\n", tVBOpt.CUSTOM_MAPPING_X);
    fprintf(f, "custom_mapping_b=%d\n", tVBOpt.CUSTOM_MAPPING_B);
    fprintf(f, "custom_mapping_y=%d\n", tVBOpt.CUSTOM_MAPPING_Y);
    fprintf(f, "custom_mapping_start=%d\n", tVBOpt.CUSTOM_MAPPING_START);
    fprintf(f, "custom_mapping_select=%d\n", tVBOpt.CUSTOM_MAPPING_SELECT);
    fprintf(f, "custom_mapping_l=%d\n", tVBOpt.CUSTOM_MAPPING_L);
    fprintf(f, "custom_mapping_r=%d\n", tVBOpt.CUSTOM_MAPPING_R);
    fprintf(f, "custom_mapping_zl=%d\n", tVBOpt.CUSTOM_MAPPING_ZL);
    fprintf(f, "custom_mapping_zr=%d\n", tVBOpt.CUSTOM_MAPPING_ZR);
    fprintf(f, "n3ds_speedup=%d\n", tVBOpt.N3DS_SPEEDUP);
    fprintf(f, "homepath=%s\n", tVBOpt.HOME_PATH);
    fprintf(f, "[touch]\n");
    fprintf(f, "ax=%d\n", tVBOpt.TOUCH_AX);
    fprintf(f, "ay=%d\n", tVBOpt.TOUCH_AY);
    fprintf(f, "bx=%d\n", tVBOpt.TOUCH_BX);
    fprintf(f, "by=%d\n", tVBOpt.TOUCH_BY);
    fprintf(f, "padx=%d\n", tVBOpt.TOUCH_PADX);
    fprintf(f, "pady=%d\n", tVBOpt.TOUCH_PADY);
    fprintf(f, "pausex=%d\n", tVBOpt.PAUSE_RIGHT);
    fprintf(f, "ff_mode=%d\n", tVBOpt.FF_TOGGLE);

    fclose(f);
    return 0;
}
