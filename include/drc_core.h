#ifndef DRC_CORE_H
#define DRC_CORE_H

#include "vb_types.h"
#include "arm_emit.h"

#define CACHE_SIZE  0x100000
#define MAX_INST    4096
#define ARM_CACHE_REG_START 4
#define ARM_NUM_CACHE_REGS 6
#define MAX_NUM_BLOCKS 4096

enum {
    DRC_ERR_BAD_ENTRY   = 1,
    DRC_ERR_BAD_PC      = 2,
    DRC_ERR_NO_DYNAREC  = 3,
    DRC_ERR_NO_BLOCKS   = 4,
    DRC_ERR_CACHE_FULL  = 5,
};

enum {
    DRC_RELOC_DIVSI     = 0,
    DRC_RELOC_MODSI     = 1,
    DRC_RELOC_UDIVSI    = 2,
    DRC_RELOC_UMODSI    = 3,
    DRC_RELOC_RBYTE     = 4,
    DRC_RELOC_RHWORD    = 5,
    DRC_RELOC_RWORD     = 6,
    DRC_RELOC_WBYTE     = 7,
    DRC_RELOC_WHWORD    = 8,
    DRC_RELOC_WWORD     = 9,
    DRC_RELOC_FPP       = 10,
    DRC_RELOC_BSTR      = 26,
};

#define END_BLOCK 0xFF
typedef struct {
    WORD phys_offset;
    WORD virt_loc;
    WORD size;
    WORD cycles;
    bool free;
    // We can use ARM_NUM_CACHE_REGS registers at a time, r4-r10, and r11 will
    // have the address of v810_state
    // reg_map[0] would have the VB register that is mapped to r4
    BYTE reg_map[7];
    WORD start_pc;
    WORD end_pc; // The address of the last instruction in the block
} exec_block;

typedef struct {
    WORD PC;
    BYTE opcode;
    BYTE reg1, reg2;
    WORD imm;
    HWORD start_pos;
    BYTE trans_size;
    int branch_offset;
    bool save_flags;
    bool busywait;
    bool is_branch_target;
} v810_instruction;

WORD* rom_block_map;
WORD* ram_block_map;
WORD* rom_entry_map;
WORD* ram_entry_map;
bool* rom_data_code_map;
bool* ram_data_code_map;
BYTE reg_usage[32];
extern WORD* cache_start;
extern WORD* cache_pos;
exec_block* block_ptr_start;
extern void* cache_dump_bin;

int __divsi3(int a, int b);
int __modsi3(int a, int b);
unsigned int __udivsi3(unsigned int a, unsigned int b);
unsigned int __umodsi3(unsigned int a, unsigned int b);

void drc_mapRegs(exec_block *block);
BYTE drc_getPhysReg(BYTE vb_reg, BYTE reg_map[]);

void drc_scanBlockBounds(WORD *p_start_PC, WORD *p_end_PC);
unsigned int drc_decodeInstructions(exec_block *block, v810_instruction *inst_cache, WORD start_PC, WORD end_PC);
int drc_translateBlock();
void drc_executeBlock(WORD* entrypoint, exec_block* block);
int drc_handleInterrupts(WORD cpsr, WORD* PC);
void drc_relocTable(void);
void drc_clearCache(void);

WORD* drc_getEntry(WORD loc, exec_block **p_block);
void drc_setEntry(WORD loc, WORD *entry, exec_block *block);
exec_block* drc_getNextBlockStruct();

void drc_init();
void drc_reset();
void drc_exit();
int drc_run();
void drc_loadSavedCache();
void drc_dumpCache(char* filename);
void drc_dumpDebugInfo();

#endif //DRC_CORE_H
