#ifndef VB_SET_H_
#define VB_SET_H_

#define d_VGA   0
#define d_VESA1 1
#define d_VESA2 2

#define dm_NORMAL		0
#define dm_RedBlue	1
#define dm_INTERLACED	2
#define dm_OVRUNDR	3
#define dm_SIDESIDE	4
#define dm_CYBERSCOPE	5

#define pal_NORMAL	0
#define pal_RED		1
#define pal_RB          2
#define pal_RG          3
#define pal_RBG         4

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

//~ void setOptions(int argc, char *argv[]);
void setDefaults(void);

extern VB_OPT         tVBOpt;
extern int            vbkey[15];

#endif

