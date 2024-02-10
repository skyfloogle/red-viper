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

void setDefaults(void) {
    // Set up the Defaults
    tVBOpt.MAXCYCLES = 400;
    tVBOpt.FRMSKIP  = 0;
    tVBOpt.DSPMODE  = DM_3D;
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
    tVBOpt.SOUND    = 0;
    tVBOpt.DSP2X    = 0;
    tVBOpt.DYNAREC  = 1;
    tVBOpt.FASTFORWARD = 0;
    tVBOpt.RENDERMODE = 1;

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
#endif
}

// inih handler
static int handler(void* user, const char* section, const char* name,
                   const char* value) {
    VB_OPT* pconfig = (VB_OPT*)user;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("vbopt", "maxcycles")) {
    } else if (MATCH("vbopt", "frmskip")) {
        pconfig->FRMSKIP = atoi(value);
    } else if (MATCH("vbopt", "dspmode")) {
        pconfig->DSPMODE = atoi(value);
    } else if (MATCH("vbopt", "dspswap")) {
        pconfig->DSPSWAP = atoi(value);
    } else if (MATCH("vbopt", "dsp2x")) {
        pconfig->DSP2X = atoi(value);
    } else if (MATCH("vbopt", "palmode")) {
        pconfig->PALMODE = atoi(value);
    } else if (MATCH("vbopt", "debug")) {
        pconfig->DEBUG = atoi(value);
    } else if (MATCH("vbopt", "stdout")) {
        pconfig->STDOUT = atoi(value);
    } else if (MATCH("vbopt", "bfactor")) {
        pconfig->BFACTOR = atoi(value);
    } else if (MATCH("vbopt", "scr_x")) {
        pconfig->SCR_X = atoi(value);
    } else if (MATCH("vbopt", "scr_y")) {
        pconfig->SCR_Y = atoi(value);
    } else if (MATCH("vbopt", "fixpal")) {
        pconfig->FIXPAL = atoi(value);
    } else if (MATCH("vbopt", "disasm")) {
        pconfig->DISASM = atoi(value);
    } else if (MATCH("vbopt", "scr_mode")) {
        pconfig->SCR_MODE = atoi(value);
    } else if (MATCH("vbopt", "sound")) {
        pconfig->SOUND = atoi(value);
    } else if (MATCH("vbopt", "dynarec")) {
        pconfig->DYNAREC = atoi(value);
    } else if (MATCH("keys", "lup")) {
    } else if (MATCH("keys", "ldown")) {
    } else if (MATCH("keys", "lleft")) {
    } else if (MATCH("keys", "lright")) {
    } else if (MATCH("keys", "rup")) {
    } else if (MATCH("keys", "rdown")) {
    } else if (MATCH("keys", "rleft")) {
    } else if (MATCH("keys", "rright")) {
    } else if (MATCH("keys", "a")) {
    } else if (MATCH("keys", "b")) {
    } else if (MATCH("keys", "start")) {
    } else if (MATCH("keys", "select")) {
    } else if (MATCH("keys", "l")) {
    } else if (MATCH("keys", "r")) {
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
    fprintf(f, "frmskip=%d\n", tVBOpt.FRMSKIP);
    fprintf(f, "dspmode=%d\n", tVBOpt.DSPMODE);
    fprintf(f, "dspswap=%d\n", tVBOpt.DSPSWAP);
    fprintf(f, "palmode=%d\n", tVBOpt.PALMODE);
    fprintf(f, "debug=%d\n", tVBOpt.DEBUG);
    fprintf(f, "stdout=%d\n", tVBOpt.STDOUT);
    fprintf(f, "bfactor=%d\n", tVBOpt.BFACTOR);
    fprintf(f, "scr_x=%d\n", tVBOpt.SCR_X);
    fprintf(f, "scr_y=%d\n", tVBOpt.SCR_Y);
    fprintf(f, "scr_mode=%d\n", tVBOpt.SCR_MODE);
    fprintf(f, "fixpal=%d\n", tVBOpt.FIXPAL);
    fprintf(f, "disasm=%d\n", tVBOpt.DISASM);
    fprintf(f, "sound=%d\n", tVBOpt.SOUND);
    fprintf(f, "dsp2x=%d\n\n", tVBOpt.DSP2X);
    fprintf(f, "dynarec=%d\n\n", tVBOpt.DYNAREC);

    fclose(f);
    return 0;
}
