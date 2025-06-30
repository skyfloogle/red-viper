#ifndef DRC_CORE_H
#define DRC_CORE_H

#include "vb_types.h"
#include "arm_emit.h"

#if __ARM_ARCH == 6
#define DRC_AVAILABLE true
#else
#define DRC_AVAILABLE false
#endif

#define MAX_ROM_SIZE 0x1000000
#define BLOCK_MAP_COUNT (MAX_ROM_SIZE / 2 / 2)
#define CACHE_SIZE  0x200000
#define MAX_V810_INST 8192
#define MAX_ARM_INST  32768
#define ARM_CACHE_REG_START 4
#define ARM_NUM_CACHE_REGS 6
#define MAX_NUM_BLOCKS 4096

#if MAX_ARM_INST >= 65536
#error "MAX_ARM_INST can't be more than 64K"
#endif

enum {
    DRC_ERR_BAD_ENTRY   = 1,
    DRC_ERR_BAD_PC      = 2,
    DRC_ERR_NO_DYNAREC  = 3,
    DRC_ERR_NO_BLOCKS   = 4,
    DRC_ERR_CACHE_FULL  = 5,
    DRC_ERR_BAD_INST    = 6,
};

enum {
    DRC_RELOC_IDIVMOD   = 0,
    DRC_RELOC_UIDIVMOD  = 1,
    DRC_RELOC_RBYTE     = 2,
    DRC_RELOC_RHWORD    = 3,
    DRC_RELOC_RWORD     = 4,
    DRC_RELOC_WBYTE     = 5,
    DRC_RELOC_WHWORD    = 6,
    DRC_RELOC_WWORD     = 7,
    DRC_RELOC_FPP       = 8,
    DRC_RELOC_BSTR      = 24,
    DRC_RELOC_GOLFHACK  = 40,
};

#define END_BLOCK 0xFF
typedef struct {
    WORD *phys_offset;
    WORD virt_loc;
    WORD size;
    WORD cycles;
    // We can use ARM_NUM_CACHE_REGS registers at a time, r4-r10, and r11 will
    // have the address of v810_state
    // reg_map & 0x1F would have the VB register that is mapped to r4
    WORD reg_map;
    bool free;
    WORD start_pc;
    WORD end_pc; // The address of the last instruction in the block
} exec_block;

typedef struct {
    WORD PC;
    WORD imm;
    BYTE opcode;
    BYTE reg1, reg2;
    HWORD start_pos;
    int branch_offset;
    BYTE trans_size;
    bool save_flags;
    bool busywait;
    bool is_branch_target;
} v810_instruction;

extern WORD* cache_start;
extern WORD* cache_pos;

int __divsi3(int a, int b);
int __modsi3(int a, int b);
unsigned int __udivsi3(unsigned int a, unsigned int b);
unsigned int __umodsi3(unsigned int a, unsigned int b);

int drc_loop(void);
int drc_handleInterrupts(WORD cpsr, WORD* PC);
void drc_relocTable(void);
void drc_clearCache(void);

WORD* drc_getEntry(WORD loc, exec_block **p_block);
void drc_setEntry(WORD loc, WORD *entry, exec_block *block);
exec_block* drc_getNextBlockStruct(void);

void drc_init(void);
void drc_reset(void);
void drc_exit(void);
int drc_run(void);
void drc_loadSavedCache(void);
void drc_dumpCache(char* filename);
void drc_dumpDebugInfo(int code);

#endif //DRC_CORE_H
