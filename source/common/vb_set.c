#include <string.h>
#include <stdlib.h>

#ifdef __3DS__
#include <3ds.h>
#endif

#include "inih/ini.h"
#include "vb_types.h"
#include "vb_set.h"

VB_OPT  tVBOpt;
int     vbkey[15];

void setDefaults(void) {
    // Set up the Defaults
    tVBOpt.MAXCYCLES = 512;
    tVBOpt.FRMSKIP  = 0;
    tVBOpt.DSPMODE  = DM_NORMAL;
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

    // Default keys
#ifdef __3DS__
    vbkey[VB_KCFG_LUP] = KEY_DUP;
    vbkey[VB_KCFG_LDOWN] = KEY_DDOWN;
    vbkey[VB_KCFG_LLEFT] = KEY_DLEFT;
    vbkey[VB_KCFG_LRIGHT] = KEY_DRIGHT;

    vbkey[VB_KCFG_RUP] = KEY_CSTICK_UP;
    vbkey[VB_KCFG_RDOWN] = KEY_CSTICK_DOWN;
    vbkey[VB_KCFG_RLEFT] = KEY_CSTICK_LEFT;
    vbkey[VB_KCFG_RRIGHT] = KEY_CSTICK_RIGHT;

    vbkey[VB_KCFG_A] = KEY_A;
    vbkey[VB_KCFG_B] = KEY_B;

    vbkey[VB_KCFG_START] = KEY_START;
    vbkey[VB_KCFG_SELECT] = KEY_SELECT;

    vbkey[VB_KCFG_L] = KEY_L;
    vbkey[VB_KCFG_R] = KEY_R;
#endif
}

// inih handler
static int handler(void* user, const char* section, const char* name,
                   const char* value) {
    VB_OPT* pconfig = (VB_OPT*)user;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0
    if (MATCH("vbopt", "maxcycles")) {
        pconfig->MAXCYCLES = atoi(value);
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
        vbkey[VB_KCFG_LUP] = atoi(value);
    } else if (MATCH("keys", "ldown")) {
        vbkey[VB_KCFG_LDOWN] = atoi(value);
    } else if (MATCH("keys", "lleft")) {
        vbkey[VB_KCFG_LLEFT] = atoi(value);
    } else if (MATCH("keys", "lright")) {
        vbkey[VB_KCFG_LRIGHT] = atoi(value);
    } else if (MATCH("keys", "rup")) {
        vbkey[VB_KCFG_RUP] = atoi(value);
    } else if (MATCH("keys", "rdown")) {
        vbkey[VB_KCFG_RDOWN] = atoi(value);
    } else if (MATCH("keys", "rleft")) {
        vbkey[VB_KCFG_RLEFT] = atoi(value);
    } else if (MATCH("keys", "rright")) {
        vbkey[VB_KCFG_RRIGHT] = atoi(value);
    } else if (MATCH("keys", "a")) {
        vbkey[VB_KCFG_A] = atoi(value);
    } else if (MATCH("keys", "b")) {
        vbkey[VB_KCFG_B] = atoi(value);
    } else if (MATCH("keys", "start")) {
        vbkey[VB_KCFG_START] = atoi(value);
    } else if (MATCH("keys", "select")) {
        vbkey[VB_KCFG_SELECT] = atoi(value);
    } else if (MATCH("keys", "l")) {
        vbkey[VB_KCFG_L] = atoi(value);
    } else if (MATCH("keys", "r")) {
        vbkey[VB_KCFG_R] = atoi(value);
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
    fprintf(f, "maxcycles=%d\n", tVBOpt.MAXCYCLES);
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

    fprintf(f, "[keys]\n");
    fprintf(f, "lup=%d\n", vbkey[VB_KCFG_LUP]);
    fprintf(f, "ldown=%d\n", vbkey[VB_KCFG_LDOWN]);
    fprintf(f, "lleft=%d\n", vbkey[VB_KCFG_LLEFT]);
    fprintf(f, "lright=%d\n", vbkey[VB_KCFG_LRIGHT]);
    fprintf(f, "rup=%d\n", vbkey[VB_KCFG_RUP]);
    fprintf(f, "rdown=%d\n", vbkey[VB_KCFG_RDOWN]);
    fprintf(f, "rleft=%d\n", vbkey[VB_KCFG_RLEFT]);
    fprintf(f, "rright=%d\n", vbkey[VB_KCFG_RRIGHT]);
    fprintf(f, "a=%d\n", vbkey[VB_KCFG_A]);
    fprintf(f, "b=%d\n", vbkey[VB_KCFG_B]);
    fprintf(f, "start=%d\n", vbkey[VB_KCFG_START]);
    fprintf(f, "select=%d\n", vbkey[VB_KCFG_SELECT]);
    fprintf(f, "l=%d\n", vbkey[VB_KCFG_L]);
    fprintf(f, "r=%d\n", vbkey[VB_KCFG_R]);

    fclose(f);
    return 0;
}
