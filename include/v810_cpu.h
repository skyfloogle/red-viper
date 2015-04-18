////////////////////////////////////////////////////////////////
// Defines for the V810 CPU

#ifndef V810_CPU_H_
#define V810_CPU_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "vb_types.h"
#include "arm_emit.h"

//System Register Defines (these are the only valid system registers!)
#define EIPC     0       //Exeption/Interupt PC
#define EIPSW    1       //Exeption/Interupt PSW

#define FEPC     2       //Fatal Error PC
#define FEPSW    3       //Fatal Error PSW

#define ECR      4       //Exception Cause Register
#define PSW      5       //Program Status Word
#define PIR      6       //Processor ID Register
#define TKCW     7       //Task Controll Word
#define CHCW     24      //Cashe Controll Word
#define ADDTRE   25      //ADDTRE

//PSW Specifics
#define PSW_IA  0xF0000 // All Interupt bits...
#define PSW_I3  0x80000
#define PSW_I2  0x40000
#define PSW_I1  0x20000
#define PSW_I0  0x10000

#define PSW_NP  0x08000
#define PSW_EP  0x04000

#define PSW_ID  0x01000
#define PSW_DP  0x00800
#define PSW_SAT 0x00400

#define PSW_CY  0x00008
#define PSW_OV  0x00004
#define PSW_S   0x00002
#define PSW_Z   0x00001

//condition codes
#define COND_V  0
#define COND_C  1
#define COND_Z  2
#define COND_NH 3
#define COND_S  4
#define COND_T  5
#define COND_LT 6
#define COND_LE 7
#define COND_NV 8
#define COND_NC 9
#define COND_NZ 10
#define COND_H  11
#define COND_NS 12
#define COND_F  13
#define COND_GE 14
#define COND_GT 15

#define END_BLOCK 0xFF

typedef struct {
    WORD* phys_loc;
    WORD virt_loc;
    WORD size;
    WORD cycles;
    BYTE jmp_reg;
    // We can use 7 registers at a time, r4-r10, and r11 will have the address
    // of v810_state
    // reg_map[0] would have the VB register that is mapped to r4
    BYTE reg_map[7];
    WORD end_pc; // The address of the last instruction in the block
} exec_block;

typedef struct {
    WORD* location;
    BYTE opcode;
    BYTE reg1, reg2;
    WORD imm;
} v810_instruction;

typedef struct {
    WORD P_REG[32]; // Main program reg pr0-pr31
    // When reading and writing the registers before and after executing a
    // block we need a place to store crap (the "33rd" register)
    WORD TMP_REG;
    WORD PC;
    WORD flags;
    WORD S_REG[32]; // System registers sr0-sr31
} cpu_state;

cpu_state* v810_state;

void v810_executeBlock(exec_block* block);

void drc_dumpCache(char* filename);

///////////////////////////////////////////////////////////////////
// Defines for memory and IO acces
// Grabed From StarScream Source

typedef struct {
    WORD lowaddr;  // Start of ram
    WORD highaddr; // end of ram
    WORD off;              // Displacement... (off+addr = pmempry...)
    BYTE *pmemory; // Pointer to memory
} V810_MEMORYFETCH;

typedef struct {
    WORD lowaddr;  // Start of ram
    WORD highaddr; // end of ram
    BYTE  (*rfuncb)(WORD);  // Pointer to the Register Read func
    void  (*wfuncb)(WORD, BYTE);    // Pointer to the Register Write func
    HWORD (*rfunch)(WORD);  // Pointer to the Register Read func
    void  (*wfunch)(WORD, HWORD);   // Pointer to the Register Write func
    WORD  (*rfuncw)(WORD);  // Pointer to the Register Read func
    void  (*wfuncw)(WORD, WORD);    // Pointer to the Register Write func
} V810_REGFETCH;

///////////////////////////////////////////////////////////////////
// Define CPU Globals

extern WORD P_REG[32];  // Main program reg pr0-pr31
extern WORD S_REG[32];  // System registers sr0-sr31
extern WORD PC;         // Program Counter
extern const BYTE opcycle[0x50]; //clock cycles

//DEBUG Globals :P

// Uncomment this line to include the FrameBuffer hack, for Yeti3D, etc!
//#define FBHACK

//////////////////////////////////////////////////////////////////////////
// Define CPU Functions

// Reset the registers
void v810_reset();

// Trace num instructions
int v810_trc();

// Generate Interupt #n
void v810_int(WORD iNum);

// Generate Exception #n
void v810_exp(WORD iNum, WORD eCode);

void serviceInt(unsigned int cycles);
int serviceDisplayInt(unsigned int cycles);

#endif
