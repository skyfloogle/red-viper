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

//Global Options list
typedef struct VB_OPT {
    int   FRMSKIP; //Frame Skip of cource
    int   DSPMODE; //Normal, 3D, etc
    int   DSPSWAP; //Swap 3D Efect, 0 normal, 1 swap
    int   DSP2X;   //Double screen size
    int   PALMODE; //Select A Pallet Type:0-normal, 1-red,etc...
    int   DEBUG;   //Releas vs Debug
    int   STDOUT;  //File vs Screen (debug)
    int   BFACTOR; //User adjustible brightness (not implemented)
    int   SCR_X;   //Display Dimentions
    int   SCR_Y;
    int   FIXPAL;	//Use a fixed pallet, no brightness...
    int   DISASM;   //Interactive dissasembly of all executed instructions...
    int   SCR_MODE; //0-VGA, 1-VESA1, 2-VESA2
    int   SOUND;
    char *ROM_NAME; //Path\Name of game to open
    char *PROG_NAME; //Path\Name of program
    unsigned long CRC32; //CRC32 of ROM
} VB_OPT;

//optionfilename, filled with option file name and full path upon startup
//~ char optionfilename[256];

void setDefaults(void);

extern VB_OPT tVBOpt;
extern int vbkey[15];

#endif
