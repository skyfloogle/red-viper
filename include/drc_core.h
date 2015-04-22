#ifndef DRC_CORE_H
#define DRC_CORE_H

#include "vb_types.h"
#include "arm_emit.h"

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
    WORD PC;
    BYTE opcode;
    BYTE reg1, reg2;
    WORD imm;
} v810_instruction;

exec_block** block_map = NULL;
WORD** entry_map = NULL;

void v810_executeBlock(WORD* entrypoint, exec_block* block);
WORD* v810_getEntry(WORD loc, exec_block** block);
void v810_setEntry(WORD loc, WORD* entry, exec_block* block);
void drc_dumpCache(char* filename);

#endif //DRC_CORE_H
