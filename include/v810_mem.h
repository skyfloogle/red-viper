#ifndef V810_MEM_H
#define V810_MEM_H

#include "v810_cpu.h"

// Memory Structure for the VIP Reg (Could have done it with an array
// but this is pretier...)
typedef struct{
    HWORD INTPND;
    HWORD INTENB;
    HWORD INTCLR;
    HWORD DPSTTS;
    HWORD DPCTRL;
    HWORD BRTA;
    HWORD BRTB;
    HWORD BRTC;
    HWORD REST;
    HWORD FRMCYC;
    HWORD XPSTTS;
    HWORD XPCTRL;
    HWORD tDisplayedFB;
    HWORD tFrame;
    HWORD SPT[4];
    BYTE GPLT[4];
    BYTE JPLT[4];
    HWORD BKCOL;
    int frametime;
    // timing
    WORD lastdisp;
    WORD lastdraw;
    BYTE rowcount;
    bool drawing;
    bool displaying;
    bool newframe;
} V810_VIPREGDAT;

typedef struct {
    BYTE SCR;       //Serial Controll Reg,  0x02000028
    BYTE WCR;       //Wait Controll Reg,    0x02000024
    BYTE TCR;       //Timer Controll Reg,   0x02000020
    BYTE THB;       //Timer Higher Byte,    0x0200001C
    BYTE TLB;       //Timer Lower Byte,     0x02000018
    BYTE ticks;
    HWORD tTHW;     //Timer TempHWord, 	not publicly visible
    WORD lasttime;
    WORD lastinput;
    SWORD tCount;  //Timer Counter register, not publicly visible
    bool tInt;
    BYTE SHB;       //Serial Higher Byte,   0x02000014  //Read Only
    BYTE SLB;       //Serial Lower Byte,    0x02000010  //Read Only
    BYTE CDRR;      //Com Recv Data Reg,    0x0200000C  //Read Only
    BYTE CDTR;      //Com Trans Data Reg,   0x02000008
    BYTE CCSR;      //Com Cnt Stat Reg,     0x02000004
    BYTE CCR;       //Com Controll Reg,     0x02000000
    bool cLock;     //Lock next communication event
    HWORD hwRead;   //Hardware input read timer
    bool cInt;      //Communication interrupt
    bool ccInt;     //Signal interrupt
    WORD lastsync;  //Multiplayer emulator sync
    WORD nextcomm;  //Next communication event
} V810_HREGDAT;

// VB state
typedef struct {
    cpu_state v810_state;
    V810_MEMORYFETCH V810_DISPLAY_RAM;
    V810_MEMORYFETCH V810_SOUND_RAM;
    V810_MEMORYFETCH V810_VB_RAM;
    V810_MEMORYFETCH V810_GAME_RAM;
    V810_VIPREGDAT   tVIPREG;
    V810_HREGDAT     tHReg;
} VB_STATE;

extern VB_STATE *vb_state;

extern VB_STATE vb_players[2];
extern bool is_multiplayer;
extern bool emulating_self;
extern int my_player_id;
extern int emulated_player_id;

extern V810_MEMORYFETCH V810_ROM1;

extern int is_sram; //Flag if writes to sram...

// Memory functions
uint64_t mem_nop(void);

uint64_t mem_vip_rbyte(WORD addr);
uint64_t mem_vip_rhword(WORD addr);
uint64_t mem_vip_rword(WORD addr);
WORD mem_vip_wbyte(WORD addr, WORD data);
WORD mem_vip_whword(WORD addr, WORD data);
WORD mem_vip_wword(WORD addr, WORD data);

WORD mem_vsu_write(WORD addr, WORD data);

uint64_t mem_hw_read(WORD addr);
WORD mem_hw_write(WORD addr, WORD data);

uint64_t mem_wram_rbyte(WORD addr);
uint64_t mem_wram_rhword(WORD addr);
uint64_t mem_wram_rword(WORD addr);
WORD mem_wram_wbyte(WORD addr, WORD data);
WORD mem_wram_whword(WORD addr, WORD data);
WORD mem_wram_wword(WORD addr, WORD data);

uint64_t mem_sram_rbyte(WORD addr);
uint64_t mem_sram_rhword(WORD addr);
uint64_t mem_sram_rword(WORD addr);
WORD mem_sram_wbyte(WORD addr, WORD data);
WORD mem_sram_whword(WORD addr, WORD data);
WORD mem_sram_wword(WORD addr, WORD data);

uint64_t mem_rom_rbyte(WORD addr);
uint64_t mem_rom_rhword(WORD addr);
uint64_t mem_rom_rword(WORD addr);

// Memory read functions
uint64_t mem_rbyte(WORD addr);
uint64_t mem_rhword(WORD addr);
uint64_t mem_rword(WORD addr);

// Memory write functions
WORD mem_wbyte(WORD addr, WORD data);
WORD mem_whword(WORD addr, WORD data);
WORD mem_wword(WORD addr, WORD data);

#endif
