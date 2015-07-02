#include <3ds.h>

#include "vb_types.h"
#include "vb_set.h"

VB_OPT  tVBOpt;
int     vbkey[15];

void setDefaults(void) {
    //Set up the Defaults
    tVBOpt.FRMSKIP  = 0;
    tVBOpt.DSPMODE  = DM_NORMAL;
    tVBOpt.DSPSWAP  = 0;
    tVBOpt.PALMODE  = PAL_NORMAL;
    tVBOpt.DEBUG    = 0;
    tVBOpt.STDOUT   = 0;
    tVBOpt.BFACTOR  = 64;
    tVBOpt.SCR_X    = 400;
    tVBOpt.SCR_Y    = 240;
    tVBOpt.SCR_MODE = 0;
    tVBOpt.FIXPAL   = 0;
    tVBOpt.DISASM   = 0;
    tVBOpt.SOUND    = 0;
    tVBOpt.DSP2X    = 0;

    //Default keys
    vbkey[0] = KEY_DUP;     // L Up
    vbkey[1] = KEY_DDOWN;   // L Down
    vbkey[2] = KEY_DLEFT;   // L Left
    vbkey[3] = KEY_DRIGHT;  // L Right

    vbkey[4] = KEY_CSTICK_UP;       // R Up
    vbkey[5] = KEY_CSTICK_DOWN;     // R Down
    vbkey[6] = KEY_CSTICK_LEFT;     // R Left
    vbkey[7] = KEY_CSTICK_RIGHT;    // R Right

    vbkey[8] = KEY_A;       // A
    vbkey[9] = KEY_B;       // B

    vbkey[10] = KEY_START;  // Start
    vbkey[11] = KEY_SELECT; // Select

    vbkey[12] = KEY_L;      // L Trigger
    vbkey[13] = KEY_R;      // R Trigger
}

// TODO: Read options from file
//~ int setFileOptions (void) {
//~ int i=0;
//~ int j=0;
//~ int k=0;
//~ int is_string=0;
//~ int optionValue;
//~ char optionSValue[81];
//~ char lineString[81];
//~ char optionString[81];
//~ char tempString[81];
//~ 
//~ FILE *optionsFile = fopen(optionfilename, "r");
//~ 
//~ if(optionsFile==0) {	return 0; }
//~ 
//~ while(1) {
//~ i=0;
//~ j=0;
//~ for(i=0;i<81;i++) {
//~ lineString[i] = '\0';
//~ tempString[i] = '\0';
//~ optionString[i] = '\0';
//~ optionSValue[i] = '\0';
//~ }
//~ i=0;
//~ fgets(lineString, 70, optionsFile);
//~ 
//~ if(feof(optionsFile)) { break; }
//~ 
//~ if(lineString[0]=='S') { is_string=1; }
//~ else { is_string=0; }
//~ 
//~ while(1) {
//~ if(lineString[i] == '=') {
//~ if(!is_string) {
//~ optionValue = ((int)lineString[i+1]-48) * 1000 + ((int)lineString[i+2]-48) * 100 + ((int)lineString[i+3]-48) * 10 + ((int)lineString[i+4]-48);
//~ break;
//~ }
//~ 
//~ else
//~ {
//~ k=0;
//~ i++;
//~ 
//~ while(lineString[k+i]!='\n') {
//~ optionSValue[k] = lineString[k+i];
//~ k++;
//~ }
//~ break;
//~ }
//~ }
//~ 
//~ else
//~ {
//~ optionString[j] = lineString[i];
//~ j++;
//~ }
//~ 
//~ i++;
//~ }
//~ 
//~ if(strcmp("platform", optionString) == 0) {
//~ if(optionValue!=platform) {
//~ printf("Option file is for the wrong platform of Red Dragon!\n");
//~ return 1;
//~ }
//~ }
//~ if(strcmp("frmskip", optionString) == 0) { tVBOpt.FRMSKIP = optionValue; }
//~ if(strcmp("dspmode", optionString) == 0) { tVBOpt.DSPMODE = optionValue; }
//~ if(strcmp("dspswap", optionString) == 0) { tVBOpt.DSPSWAP = optionValue; }
//~ if(strcmp("dsp2x", optionString) == 0) { tVBOpt.DSP2X = optionValue; }
//~ if(strcmp("palmode", optionString) == 0) { tVBOpt.PALMODE = optionValue; }
//~ if(strcmp("scrx", optionString) == 0) { tVBOpt.SCR_X = optionValue; }
//~ if(strcmp("scry", optionString) == 0) { tVBOpt.SCR_Y = optionValue; }
//~ if(strcmp("scrmode", optionString) == 0) { tVBOpt.SCR_MODE = VBgfx_driver[optionValue]; }
//~ if(strcmp("sound", optionString) == 0) { tVBOpt.SOUND = optionValue; }
//~ if(strcmp("lu", optionString) == 0) { vbkey[0] = optionValue; }
//~ if(strcmp("ld", optionString) == 0) { vbkey[1] = optionValue; }
//~ if(strcmp("ll", optionString) == 0) { vbkey[2] = optionValue; }
//~ if(strcmp("lr", optionString) == 0) { vbkey[3] = optionValue; }
//~ if(strcmp("ru", optionString) == 0) { vbkey[4] = optionValue; }
//~ if(strcmp("rd", optionString) == 0) { vbkey[5] = optionValue; }
//~ if(strcmp("rl", optionString) == 0) { vbkey[6] = optionValue; }
//~ if(strcmp("rr", optionString) == 0) { vbkey[7] = optionValue; }
//~ if(strcmp("ba", optionString) == 0) { vbkey[8] = optionValue; }
//~ if(strcmp("bb", optionString) == 0) { vbkey[9] = optionValue; }
//~ if(strcmp("st", optionString) == 0) { vbkey[10] = optionValue; }
//~ if(strcmp("sl", optionString) == 0) { vbkey[11] = optionValue; }
//~ if(strcmp("tl", optionString) == 0) { vbkey[12] = optionValue; }
//~ if(strcmp("tr", optionString) == 0) { vbkey[13] = optionValue; }
//~ if(strcmp("bl", optionString) == 0) { vbkey[14] = optionValue; }
//~ 
//~ if(strcmp("Srompath", optionString) == 0) { strcpy(rompath, optionSValue); }
//~ }
//~ fclose(optionsFile);
//~ return 1;
//~ }
//~ 
